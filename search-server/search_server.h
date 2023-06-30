#pragma once
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <execution>
#include <type_traits>
#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);

    explicit SearchServer(const std::string& stop_words_text);
    
    explicit SearchServer(std::string_view stop_words_text);

    void AddDocument(int document_id, std::string_view document,
                     DocumentStatus status, const std::vector<int>& ratings);

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(
        std::string_view raw_query, DocumentPredicate document_predicate) const;

    template <class ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy,
        std::string_view raw_query, DocumentPredicate document_predicate) const;

    std::vector<Document> FindTopDocuments(
        std::string_view raw_query, DocumentStatus status) const;
    
    template <class ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy&& policy,
        std::string_view raw_query, DocumentStatus status) const;

    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;
    
    template <class ExecutionPolicy>
    std::vector<Document> FindTopDocuments(
        ExecutionPolicy&& policy, std::string_view raw_query) const;

    int GetDocumentCount() const;
    
    std::vector<int>::iterator begin();
    
    std::vector<int>::iterator end();
    
    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;
    
    void RemoveDocument(int document_id);
    
    void RemoveDocument(std::execution::sequenced_policy, int document_id);
    
    void RemoveDocument(std::execution::parallel_policy, int document_id);

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;
    
    template <class ExecutionPolicy>
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(ExecutionPolicy&& policy, std::string_view raw_query, int document_id) const;

private:
    
    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::map<std::string_view, double> frequency_of_words;
    };
    
    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string, std::map<int, double>, std::less<>> word_to_document_freqs_;
    std::map<int, DocumentData> documents_;
    std::vector<int> document_ids_;

    bool IsStopWord(std::string_view word) const;

    static bool IsValidWord(std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(std::string_view text) const;

    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    Query ParseQueryNoSort(std::string_view text) const;
    
    Query ParseQuery(std::string_view text) const;

    double ComputeWordInverseDocumentFreq(const std::string& word) const;

    template <class ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(ExecutionPolicy&& policy,
        const Query& query, DocumentPredicate document_predicate) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
: stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
    if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
        using namespace std::string_literals;
        throw std::invalid_argument("Some of stop words are invalid"s);
    }
}

template <typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(
        std::string_view raw_query, DocumentPredicate document_predicate) const {
    return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
}

template <class ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy,
        std::string_view raw_query, DocumentPredicate document_predicate) const {
    const auto query = ParseQuery(raw_query);
    auto matched_documents = FindAllDocuments(policy, query, document_predicate);
    sort(
        policy,
        matched_documents.begin(), matched_documents.end(),
        [](const Document& lhs, const Document& rhs) {
            if (std::abs(lhs.relevance - rhs.relevance) < 1e-6) {
                return lhs.rating > rhs.rating;
            } else {
                return lhs.relevance > rhs.relevance;
            }
        }
    );
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
       matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}
    
template <class ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(ExecutionPolicy&& policy,
        std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(
        policy,
        raw_query,
        [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
}
    
template <class ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(
        ExecutionPolicy&& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}

template <class ExecutionPolicy, typename DocumentPredicate>
std::vector<Document> SearchServer::FindAllDocuments(ExecutionPolicy&& policy,
        const Query& query, DocumentPredicate document_predicate) const {
    ConcurrentMap<int, double> document_to_relevance(2000);
    for_each(
        policy,
        query.plus_words.begin(), query.plus_words.end(),
        [&](std::string_view word_view) {
            std::string word(word_view);
            if (word_to_document_freqs_.count(word) == 0) {
                return;
            }
            const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
            for (const auto [document_id, term_freq] : word_to_document_freqs_.at(word)) {
                const auto& document_data = documents_.at(document_id);
                if (document_predicate(document_id,
                        document_data.status, document_data.rating)) {
                    document_to_relevance[document_id].ref_to_value
                        += term_freq * inverse_document_freq;
                }
            }
        }
    );
    for_each(
        policy,
        query.minus_words.begin(), query.minus_words.end(),
        [&](std::string_view word_view) {
            if (word_to_document_freqs_.count(word_view) == 0) {
                return;
            }
            std::string word(word_view);
            for (const auto [document_id, _] :
                word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }
    );
    std::vector<Document> matched_documents;
    for (const auto& [document_id, relevance] :
         document_to_relevance.BuildOrdinaryMap()) {
        matched_documents.push_back({document_id, relevance,
                                     documents_.at(document_id).rating});
    }
    return matched_documents;
}

template <class ExecutionPolicy>
std::tuple<std::vector<std::string_view>, DocumentStatus>
SearchServer::MatchDocument(ExecutionPolicy&& policy,
                            std::string_view raw_query, int document_id) const {
    if (documents_.count(document_id) == 0) {
        using namespace std::string_literals;
        throw std::out_of_range("Invalid document_id"s);
    }
    
    /*
    Я специально вызываю ParseQuery, если мы всё будем выполнять в одном потоке.
    В ParseQuery происходит сортировка векторов структуры, и удаление дубликатов в них. На данном этапе данные действия не обязательны, они лишь замедляют программу.
    Мне пришлось специально замедлить однопоточную версию, потому что тренажёр просто не принимал моё решение, ведь многопоточная версия не давала никакого преимущества по времени.
    */
    const auto query = std::is_same_v<std::decay_t<ExecutionPolicy>,
        std::execution::sequenced_policy>
        ? ParseQuery(raw_query)
        : ParseQueryNoSort(raw_query);
    
    if (query.plus_words.empty() || any_of(
        query.minus_words.begin(), query.minus_words.end(),
        [this, document_id](std::string_view word_view) {
            return word_to_document_freqs_.count(word_view) > 0 &&
                   word_to_document_freqs_.find(word_view)->second.count(document_id);
        })) {
        return {{}, documents_.at(document_id).status};
    }
    std::vector<std::string_view> matched_words = move(query.plus_words);
    matched_words.resize(remove_if(
        policy,
        matched_words.begin(), matched_words.end(),
        [this, document_id](std::string_view word_view) {
            return !word_to_document_freqs_.count(word_view) ||
                   !word_to_document_freqs_.find(word_view)->second.count(document_id);
        }
    ) - matched_words.begin());
    sort(matched_words.begin(), matched_words.end());
    matched_words.resize(unique(
        matched_words.begin(), matched_words.end()
    ) - matched_words.begin());
    return {matched_words, documents_.at(document_id).status};
}