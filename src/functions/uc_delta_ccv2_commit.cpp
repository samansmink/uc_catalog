#include "functions/uc_table_functions.hpp"
#include "storage/uc_transaction.hpp"
#include "storage/uc_catalog.hpp"
#include "storage/uc_table_entry.hpp"

namespace duckdb {

// struct UCDeltaCCV2CommitData final : public TableFunctionData {
// 	UCDeltaCCV2CommitData(Value input) : value(input) {
// 	}
// 	Value value;
// };
//

struct UCDeltaCCV2CommitState final : public GlobalTableFunctionState {
	UCDeltaCCV2CommitState() {
	}

	bool finished = false;
};

unique_ptr<GlobalTableFunctionState> UCSetCommitMessageInit(ClientContext &context,
                                                                  TableFunctionInitInput &input) {
	return make_uniq<UCDeltaCCV2CommitState>();
}

static unique_ptr<FunctionData> UCSetCommitMessageBind(ClientContext &context, TableFunctionBindInput &input,
                                                             vector<LogicalType> &return_types, vector<string> &names) {
	throw InternalException("__internal_delta_ccv2_commit_staged is only for internal use and should not be called directly");
}

void UCSetCommitMessageExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto val = output.GetValue(0,0);
	printf("Committing %s\n", val.ToString().c_str());
	output.SetCardinality(2);
	output.SetValue(0,1, Value::BOOLEAN(true));
}

UCDeltaCCV2Commit::UCDeltaCCV2Commit()
    : TableFunction("__internal_delta_ccv2_commit_staged", {LogicalType::VARCHAR}, UCSetCommitMessageExecute, UCSetCommitMessageBind, UCSetCommitMessageInit) {

}
} // namespace duckdb
