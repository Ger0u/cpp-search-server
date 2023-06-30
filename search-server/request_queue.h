#pragma once
#include <vector>
#include <string>
#include <queue>
#include "search_server.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);
    
    void NewRequest(const std::vector<Document>& documents);

    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate);

    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(const std::string& raw_query);

    int GetNoResultRequests() const;
private:
    struct QueryResult {
        QueryResult(const std::vector<Document>& documents)
        : empty(documents.empty()) {
        }
        
        bool empty;
    };
    
    const SearchServer& search_server_;
    std::deque<QueryResult> requests_;
    int size = 0;
    int num_empty_result = 0;
    const static int sec_in_day_ = 1440;
};

template <typename DocumentPredicate>
std::vector<Document> RequestQueue::AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate) {
    auto documents = search_server_.FindTopDocuments(raw_query, document_predicate);
    NewRequest(documents);
    return documents;
}