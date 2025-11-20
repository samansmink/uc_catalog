//===----------------------------------------------------------------------===//
//                         DuckDB
//
// functions/uc_table_functions.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
#include "duckdb/function/function_set.hpp"

namespace duckdb {
class UCCatalog;

class UCDeltaCCV2Commit : public TableFunction {
public:
	UCDeltaCCV2Commit();
};

} // namespace duckdb
