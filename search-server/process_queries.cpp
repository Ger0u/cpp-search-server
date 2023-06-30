#include "process_queries.h"
#include <algorithm>
#include <execution>
#include <numeric>
#include <functional>

using namespace std;

vector<vector<Document>> ProcessQueries(
    const SearchServer& search_server,
    const vector<string>& queries) {
    vector<vector<Document>> result(queries.size());
    transform(execution::par, queries.begin(), queries.end(),
              result.begin(),
              [&](const string& query) {
                  return search_server.FindTopDocuments(query);
              });
    return result;
}

vector<Document> ProcessQueriesJoined(
    const SearchServer& search_server,
    const vector<string>& queries) {
    vector<vector<Document>> documents = ProcessQueries(search_server, queries);
    vector<Document> result(transform_reduce(execution::par,
            documents.begin(), documents.end(), 0,
            plus<>{},
            [](const vector<Document>& docs) {
                return docs.size();
            }));
    auto it = result.begin();
    for (auto& document : documents) {
        it = move(execution::par, document.begin(), document.end(), it);
    }
    return result;
}