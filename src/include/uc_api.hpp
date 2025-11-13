//===----------------------------------------------------------------------===//
//                         DuckDB
//
// src/include/uc_api.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types.hpp"

namespace duckdb {
struct UCCredentials;

struct UCAPIColumnDefinition {
	string name;
	string type_text;
	idx_t precision;
	idx_t scale;
	idx_t position;
};

struct UCAPITable {
	string table_id;

	string name;
	string catalog_name;
	string schema_name;
	string table_type;
	string data_source_format;

	string storage_location;
	string delta_last_commit_timestamp;
	string delta_last_update_version;

	vector<UCAPIColumnDefinition> columns;
	unordered_map<string, string> properties;
};

struct UCAPISchema {
	string schema_name;
	string catalog_name;
};

struct UCAPITableCredentials {
	string key_id;
	string secret;
	string session_token;
};

struct UCAPICommit {
	int64_t version;
	int64_t timestamp;
	string file_name;
	int64_t file_size;
	int64_t file_modification_timestamp;
};

struct UCAPICommitsResult {
	vector<UCAPICommit> commits;
	int64_t latest_table_version;
};

class UCAPI {
public:
	//! WARNING: not thread-safe. To be called once on extension initialization
	static void InitializeCurl();

	static UCAPITableCredentials GetTableCredentials(const string &table_id, const UCCredentials &credentials);
	static string GetDefaultSchema(const UCCredentials &credentials);
	static vector<string> GetCatalogs(const string &catalog, const UCCredentials &credentials);
	static vector<UCAPITable> GetTables(const string &catalog, const string &schema, const UCCredentials &credentials);
	static vector<UCAPISchema> GetSchemas(const string &catalog, const UCCredentials &credentials);
	static vector<UCAPITable> GetTablesInSchema(const string &catalog, const string &schema,
	                                            const UCCredentials &credentials);
	static UCAPICommitsResult GetCommits(const string &table_id, const string &table_uri, const UCCredentials &credentials);
};
} // namespace duckdb
