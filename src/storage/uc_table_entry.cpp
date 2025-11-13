#include "storage/uc_catalog.hpp"
#include "storage/uc_schema_entry.hpp"
#include "storage/uc_table_entry.hpp"
#include "storage/uc_transaction.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "uc_api.hpp"
#include "../../duckdb/third_party/catch/catch.hpp"

namespace duckdb {

UCTableEntry::UCTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, CreateTableInfo &info)
    : TableCatalogEntry(catalog, schema, info) {
	this->internal = false;
}

UCTableEntry::UCTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, UCTableInfo &info)
    : TableCatalogEntry(catalog, schema, *info.create_info) {
	this->internal = false;
}

unique_ptr<BaseStatistics> UCTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	return nullptr;
}

optional_ptr<Catalog> UCTableEntry::GetInternalCatalog() {
	return internal_attached_database->GetCatalog();
}

void UCTableEntry::BindUpdateConstraints(Binder &binder, LogicalGet &, LogicalProjection &, LogicalUpdate &,
                                         ClientContext &) {
	throw NotImplementedException("BindUpdateConstraints");
}

TableFunction UCTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	auto &db = DatabaseInstance::GetDatabase(context);

	auto &system_catalog = Catalog::GetSystemCatalog(db);
	auto data = CatalogTransaction::GetSystemTransaction(db);
	auto &schema = system_catalog.GetSchema(data, DEFAULT_SCHEMA);
	auto catalog_entry = schema.GetEntry(data, CatalogType::TABLE_FUNCTION_ENTRY, "delta_scan");
	if (!catalog_entry) {
		throw InvalidInputException("Function with name \"%s\" not found in "
		                            "ExtensionLoader::GetTableFunction",
		                            name);
	}
	auto &delta_function_set = catalog_entry->Cast<TableFunctionCatalogEntry>();

	auto delta_scan_function = delta_function_set.functions.GetFunctionByArguments(context, {LogicalType::VARCHAR});
	auto &uc_catalog = catalog.Cast<UCCatalog>();

	D_ASSERT(table_data);

	if (table_data->data_source_format != "DELTA") {
		throw NotImplementedException("Table '%s' is of unsupported format '%s', ", table_data->name,
		                              table_data->data_source_format);
	}

	// CCV2
	Value ccv2_value;
	auto ccv2_lookup = table_data->properties.find("delta.feature.catalogOwned-preview");
	if (ccv2_lookup != table_data->properties.end() && ccv2_lookup->second == "supported") {
		auto commits = UCAPI::GetCommits(table_data->table_id, table_data->storage_location, uc_catalog.credentials);

		vector<Value> commit_values;
		for (const auto &commit : commits.commits) {
			child_list_t<Value> commit_struct;

			commit_struct.push_back(make_pair("version", Value::BIGINT(commit.version)));
			commit_struct.push_back(make_pair("timestamp", Value::BIGINT(commit.timestamp)));
			commit_struct.push_back(make_pair("file_name", Value(table_data->storage_location + "/_delta_log/_staged_commits/" + commit.file_name)));
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


	// Set the S3 path as input to table function
	vector<Value> inputs = {table_data->storage_location};

	if (table_data->storage_location.find("file://") != 0) {
		auto &secret_manager = SecretManager::Get(context);
		// Get Credentials from UCAPI
		auto table_credentials = UCAPI::GetTableCredentials(table_data->table_id, uc_catalog.credentials);

		// Inject secret into secret manager scoped to this path
		CreateSecretInput input;
		input.on_conflict = OnCreateConflict::REPLACE_ON_CONFLICT;
		input.persist_type = SecretPersistType::TEMPORARY;
		input.name = "__internal_uc_" + table_data->table_id;
		input.type = "s3";
		input.provider = "config";
		input.options = {
		    {"key_id", table_credentials.key_id},
		    {"secret", table_credentials.secret},
		    {"session_token", table_credentials.session_token},
		    {"region", uc_catalog.credentials.aws_region},
		};
		input.scope = {table_data->storage_location};

		secret_manager.CreateSecret(context, input);
	}
	named_parameter_map_t param_map;
	vector<LogicalType> return_types;
	vector<string> names;
	TableFunctionRef empty_ref;

	if (!ccv2_value.IsNull()) {
		param_map["log_tail"] = ccv2_value;
	}

	TableFunctionBindInput bind_input(inputs, param_map, return_types, names, nullptr, nullptr, delta_scan_function,
	                                  empty_ref);

	auto result = delta_scan_function.bind(context, bind_input, return_types, names);
	bind_data = std::move(result);

	return delta_scan_function;
}

virtual_column_map_t UCTableEntry::GetVirtualColumns() const {
	//! FIXME: requires changes in core to be able to delegate this
	return TableCatalogEntry::GetVirtualColumns();
}

vector<column_t> UCTableEntry::GetRowIdColumns() const {
	//! FIXME: requires changes in core to be able to delegate this
	return TableCatalogEntry::GetRowIdColumns();
}

TableStorageInfo UCTableEntry::GetStorageInfo(ClientContext &context) {
	TableStorageInfo result;
	// TODO fill info
	return result;
}

} // namespace duckdb
