#include "request_queue.h"

using namespace std;

RequestQueue::RequestQueue(const SearchServer& search_server)
: search_server_(search_server) {
}

void RequestQueue::NewRequest(const vector<Document>& documents) {
    requests_.push_back(documents);
    if (size < sec_in_day_) {
        ++size;
        if (documents.empty()) {
            ++num_empty_result;
        }
        return;
    }
    if (requests_.front().empty && !documents.empty()) {
        --num_empty_result;
    } else if (!requests_.front().empty && documents.empty()) {
        ++num_empty_result;
    }
    requests_.pop_front();
    return;
}

vector<Document> RequestQueue::AddFindRequest(const string& raw_query, DocumentStatus status) {
    auto documents = search_server_.FindTopDocuments(raw_query, status);
    NewRequest(documents);
    return documents;
}

vector<Document> RequestQueue::AddFindRequest(const string& raw_query) {
    auto documents = search_server_.FindTopDocuments(raw_query);
    NewRequest(documents);
    return documents;
}

int RequestQueue::GetNoResultRequests() const {
    return num_empty_result;
}