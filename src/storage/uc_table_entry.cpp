#include "storage/unity_catalog.hpp"
#include "storage/uc_schema_entry.hpp"
#include "storage/uc_table_entry.hpp"
#include "storage/uc_table_set.hpp"
#include "storage/uc_transaction.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"
#include "duckdb/storage/table_storage_info.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "uc_api.hpp"

namespace duckdb {

UCTableEntry::UCTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, TableInformation &table, CreateTableInfo &info)
    : TableCatalogEntry(catalog, schema, info), table(table) {
	this->internal = false;
}

unique_ptr<BaseStatistics> UCTableEntry::GetStatistics(ClientContext &context, column_t column_id) {
	return nullptr;
}

void UCTableEntry::BindUpdateConstraints(Binder &binder, LogicalGet &, LogicalProjection &, LogicalUpdate &,
                                         ClientContext &) {
	throw NotImplementedException("BindUpdateConstraints");
}

TableFunction UCTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	throw InternalException("UCTableEntry::GetScanFunction called without entry lookup info");
}

TableFunction UCTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data, const EntryLookupInfo &lookup_info) {
	auto &table_data = table.table_data;
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
		auto commits = UCAPI::GetCommits(context, table_data->table_id, table_data->storage_location, uc_catalog.credentials);

		vector<Value> commit_values;
		for (const auto &commit : commits.commits) {
			child_list_t<Value> commit_struct;

			commit_struct.push_back(make_pair("version", Value::BIGINT(commit.version)));
			commit_struct.push_back(make_pair("timestamp", Value::BIGINT(commit.timestamp)));
			commit_struct.push_back(make_pair(
				"file_name", Value(table_data->storage_location + "/_delta_log/_staged_commits/" + commit.file_name)));
			commit_struct.push_back(make_pair("file_size", Value::BIGINT(commit.file_size)));
			commit_struct.push_back(
				make_pair("file_modification_timestamp", Value::BIGINT(commit.file_modification_timestamp)));
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

	///              OLD	OLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLDOLD
	///              TODO: we currently break our ability to inktjet the log tail
	// named_parameter_map_t param_map;
	// vector<LogicalType> return_types;
	// vector<string> names;
	// TableFunctionRef empty_ref;
	//
	// if (!ccv2_value.IsNull()) {
	// 	param_map["log_tail"] = ccv2_value;
	// }
	//
	// TableFunctionBindInput bind_input(inputs, param_map, return_types, names, nullptr, nullptr, delta_scan_function,
	// 								  empty_ref);
	//
	// auto result = delta_scan_function.bind(context, bind_input, return_types, names);
	// bind_data = std::move(result);
	//
	// return delta_scan_function;
	//
	// // TODO: unused now? code was move into internalAttach
	// // Set the S3 path as input to table function
	// vector<Value> inputs = {table_data->storage_location};
	// if (!ccv2_value.IsNull()) {
	// 	param_map["log_tail"] = ccv2_value;
	// }

	// NEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEWNEW

	table.RefreshCredentials(context);
	table.InternalAttach(context);

	auto &delta_catalog = *table.GetInternalCatalog();
	//! NOTE: This has to be DEFAULT_SCHEMA, we can't use the table_data->schema
	auto &schema = delta_catalog.GetSchema(context, DEFAULT_SCHEMA);
	auto transaction = schema.GetCatalogTransaction(context);
	auto table_entry = schema.LookupEntry(transaction, lookup_info);
	D_ASSERT(table_entry);

	auto &delta_table = table_entry->Cast<TableCatalogEntry>();
	return delta_table.GetScanFunction(context, bind_data, lookup_info);

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
