#include "storage/unity_catalog.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/parser/parsed_data/attach_info.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckdb/parser/parsed_data/drop_info.hpp"
#include "duckdb/planner/operator/logical_insert.hpp"
#include "duckdb/storage/database_size.hpp"
#include "storage/uc_schema_entry.hpp"
#include "storage/uc_transaction.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

UCCatalog::UCCatalog(AttachedDatabase &db_p, const string &internal_name, AttachOptions &attach_options,
                     UCCredentials credentials, const string &default_schema, string catalog_name_p)
    : Catalog(db_p), internal_name(internal_name), access_mode(attach_options.access_mode),
      credentials(std::move(credentials)), catalog_name(std::move(catalog_name_p)), schemas(*this),
      default_schema(default_schema) {
}

UCCatalog::~UCCatalog() = default;

void UCCatalog::Initialize(bool load_builtin) {
}

optional_ptr<CatalogEntry> UCCatalog::CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) {
	if (info.on_conflict == OnCreateConflict::REPLACE_ON_CONFLICT) {
		DropInfo try_drop;
		try_drop.type = CatalogType::SCHEMA_ENTRY;
		try_drop.name = info.schema;
		try_drop.if_not_found = OnEntryNotFound::RETURN_NULL;
		try_drop.cascade = false;
		schemas.DropEntry(transaction.GetContext(), try_drop);
	}
	return schemas.CreateSchema(transaction.GetContext(), info);
}

void UCCatalog::DropSchema(ClientContext &context, DropInfo &info) {
	return schemas.DropEntry(context, info);
}

void UCCatalog::ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) {
	schemas.Scan(context, [&](CatalogEntry &schema) { callback(schema.Cast<UCSchemaEntry>()); });
}

optional_ptr<SchemaCatalogEntry> UCCatalog::LookupSchema(CatalogTransaction transaction,
                                                         const EntryLookupInfo &schema_lookup,
                                                         OnEntryNotFound if_not_found) {
	if (schema_lookup.GetEntryName() == DEFAULT_SCHEMA && default_schema != DEFAULT_SCHEMA) {
		if (default_schema.empty()) {
			throw InvalidInputException(
			    "Default schema for catalog '%s' not found. This means auto-detection of default schema failed. Please "
			    "specify a DEFAULT_SCHEMA on ATTACH: `ATTACH '..' (TYPE unity_catalog, DEFAULT_SCHEMA 'my_schema')`",
			    GetName());
		}
		return GetSchema(transaction, default_schema, if_not_found);
	}
	auto entry = schemas.GetEntry(transaction.GetContext(), schema_lookup);
	if (!entry && if_not_found != OnEntryNotFound::RETURN_NULL) {
		throw BinderException("Schema with name \"%s\" not found", schema_lookup.GetEntryName());
	}
	return reinterpret_cast<SchemaCatalogEntry *>(entry.get());
}

bool UCCatalog::InMemory() {
	return false;
}

string UCCatalog::GetDBPath() {
	return internal_name;
}

string UCCatalog::GetDefaultSchema() const {
	return default_schema;
}

void UCCatalog::OnDetach(ClientContext &context) {
	schemas.Scan(context, [&](CatalogEntry &entry) {
		auto &schema = entry.Cast<UCSchemaEntry>();
		auto &tables = schema.tables;
		tables.OnDetach(context);
	});
}

DatabaseSize UCCatalog::GetDatabaseSize(ClientContext &context) {
	if (default_schema.empty()) {
		throw InvalidInputException("Attempting to fetch the database size - but no database was provided "
		                            "in the connection string");
	}
	DatabaseSize size;
	return size;
}

void UCCatalog::ClearCache() {
	schemas.ClearEntries();
}

PhysicalOperator &UCCatalog::PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner,
                                               LogicalCreateTable &op, PhysicalOperator &plan) {
	throw NotImplementedException("UCCatalog PlanCreateTableAs");
}

PhysicalOperator &UCCatalog::PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
                                        optional_ptr<PhysicalOperator> plan) {
	auto &table_entry = op.table.Cast<UCTableEntry>();
	auto &table = table_entry.table;

	// Detect CCV2
	bool ccv2_enabled = false;
	Value ccv2_value;
	auto ccv2_lookup = table.table_data->properties.find("delta.feature.catalogOwned-preview");
	if (ccv2_lookup != table.table_data->properties.end() && ccv2_lookup->second == "supported") {
		ccv2_enabled = true;

		// Fetch the catalog-managed commits for CCV2 tables
		auto commits = UCAPI::GetCommits(context, table.table_data->table_id, table.table_data->storage_location, credentials);

		vector<Value> commit_values;
		for (const auto &commit : commits.commits) {
			child_list_t<Value> commit_struct;

			commit_struct.push_back(make_pair("version", Value::BIGINT(commit.version)));
			commit_struct.push_back(make_pair("timestamp", Value::BIGINT(commit.timestamp)));
			commit_struct.push_back(make_pair("file_name", Value(table.table_data->storage_location + "/_delta_log/_staged_commits/" + commit.file_name)));
			commit_struct.push_back(make_pair("file_size", Value::BIGINT(commit.file_size)));
			commit_struct.push_back(make_pair("file_modification_timestamp", Value::BIGINT(commit.file_modification_timestamp)));
			commit_values.push_back(Value::STRUCT(std::move(commit_struct)));
		}
		ccv2_value =
			Value::LIST(LogicalType::STRUCT(
							{
								make_pair("version", LogicalType::BIGINT),
								make_pair("timestamp", LogicalType::BIGINT),
								make_pair("file_name", LogicalType::VARCHAR), make_pair("file_size", LogicalType::BIGINT),
								make_pair("file_modification_timestamp", LogicalType::BIGINT)
							}),commit_values);
	}

	// LAZY CREATE ATTACHED DB
	// TODO: move to transaction?
	if (!table.internal_attached_database) {
		auto &db_manager = DatabaseManager::Get(context);

		// Create the attach info for the table
		AttachInfo info;
		info.name =
		    "__unity_catalog_internal_" + internal_name + "_" + table.schema.name + "_" + table_entry.name; // TODO:
		info.options = {{"type", Value("Delta")},
		                {"child_catalog_mode", Value(true)},
		                {"internal_table_name", Value(table_entry.name)},
						{"parent_catalog", Value(this->GetName())},
						{"parent_catalog_schema", Value(table.schema.name)},
						{"parent_commit", Value(ccv2_enabled)}};
		info.path = table.table_data->storage_location;

		// Pass the log_tail for CCV2 tables
		if (!ccv2_value.IsNull()) {
			info.options["log_tail"] = ccv2_value;
		}

		AttachOptions options(context.db->config.options);
		options.access_mode = AccessMode::READ_WRITE;
		options.db_type = "delta";
		auto &internal_db = table.internal_attached_database;

		// internal_db = make_shared_ptr<AttachedDatabase>(*context.db, *this, info.name, info.path, options);
		internal_db = db_manager.AttachDatabase(context, info, options);
	}

	// LOAD THE INTERNAL TABLE ENTRY
	auto internal_catalog = table.GetInternalCatalog();
	table.RefreshCredentials(context);
	return internal_catalog->PlanInsert(context, planner, op, plan);
}

PhysicalOperator &UCCatalog::PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
                                        PhysicalOperator &plan) {
	throw NotImplementedException("UCCatalog PlanDelete");
}

PhysicalOperator &UCCatalog::PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op) {
	throw NotImplementedException("UCCatalog PlanDelete");
}

PhysicalOperator &UCCatalog::PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
                                        PhysicalOperator &plan) {
	throw NotImplementedException("UCCatalog PlanUpdate");
}

unique_ptr<LogicalOperator> UCCatalog::BindCreateIndex(Binder &binder, CreateStatement &stmt, TableCatalogEntry &table,
                                                       unique_ptr<LogicalOperator> plan) {
	throw NotImplementedException("UCCatalog BindCreateIndex");
}

} // namespace duckdb
