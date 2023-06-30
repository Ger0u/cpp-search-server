#include "search_server.h"
#include <math.h>

using namespace std;


SearchServer::SearchServer(const std::string& stop_words_text)
: SearchServer(SplitIntoWords(stop_words_text)) {
}

SearchServer::SearchServer(string_view stop_words_text)
: SearchServer(SplitIntoWords(stop_words_text)) {
}

void SearchServer::AddDocument(int document_id, string_view document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    const auto words = SplitIntoWordsNoStop(document);
    const double inv_word_count = 1.0 / words.size();
    map<string_view, double> frequency_of_words;
    for (string_view word_view : words) {
        string word(word_view);
        word_to_document_freqs_[word][document_id] += inv_word_count;
        frequency_of_words[word_to_document_freqs_.find(word)->first] += inv_word_count;
    }
    documents_.insert({document_id, DocumentData{ComputeAverageRating(ratings), status, frequency_of_words}});
    document_ids_.push_back(document_id);
}

vector<Document> SearchServer::FindTopDocuments(
    string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(execution::seq, raw_query, status);
}

vector<Document> SearchServer::FindTopDocuments(string_view raw_query) const {
    return FindTopDocuments(execution::seq, raw_query);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

vector<int>::iterator SearchServer::begin() {
    return document_ids_.begin();
}
    
vector<int>::iterator SearchServer::end() {
    return document_ids_.end();
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    if (documents_.count(document_id) > 0) {
        return documents_.at(document_id).frequency_of_words;
    }
    static const map<string_view, double> map_empty;
    return map_empty;
}

void SearchServer::RemoveDocument(int document_id) {
    for (const auto [word_view, _] : documents_[document_id].frequency_of_words) {
        word_to_document_freqs_[string(word_view)].erase(document_id);
    }
    documents_.erase(document_id);
    auto it = lower_bound(document_ids_.begin(), document_ids_.end(), document_id);
    if (*it == document_id) {
        document_ids_.erase(it);
    }
}

void SearchServer::RemoveDocument(execution::sequenced_policy, int document_id) {
    RemoveDocument(document_id);
}
    
void SearchServer::RemoveDocument(execution::parallel_policy, int document_id) {
    for_each(execution::par, 
             documents_[document_id].frequency_of_words.begin(),
             documents_[document_id].frequency_of_words.end(),
            [this, document_id](const auto& el) {
                word_to_document_freqs_[string(el.first)].erase(document_id);
            });
    documents_.erase(document_id);
    auto it = lower_bound(document_ids_.begin(), document_ids_.end(), document_id);
    if (*it == document_id) {
        document_ids_.erase(it);
    }
}

tuple<vector<string_view>, DocumentStatus>
SearchServer::MatchDocument(string_view raw_query, int document_id) const {
    return MatchDocument(execution::seq, raw_query, document_id);
}

bool SearchServer::IsStopWord(string_view word_view) const {
    return stop_words_.count(word_view);
}

bool SearchServer::IsValidWord(string_view word_view) {
    return none_of(word_view.begin(), word_view.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(string_view text) const {
    vector<string_view> words;
    for (string_view word_view : SplitIntoWords(text)) {
        if (!IsValidWord(word_view)) {
            throw invalid_argument("Word "s + string(word_view) + " is invalid"s);
        }
        if (!IsStopWord(word_view)) {
            words.push_back(word_view);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    string_view word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word.remove_prefix(1);
    }
    if (word.empty() || word[0] == '-' || !IsValidWord(word)) {
        throw invalid_argument("Query word "s + string(text) + " is invalid"s);
    }
    return {word, is_minus, IsStopWord(word)};
}

SearchServer::Query SearchServer::ParseQueryNoSort(string_view text) const {
    //Я внедрил алгоритм метода SplitIntoWords сюда, не использовав сам метод,
    //это дало небольшой выигрыш по времени
    Query result;
    size_t space;
    while (true) {
        space = text.find(' ');
        if (space >= string_view::npos) {
            if (!text.empty()) {
                QueryWord query_word = ParseQueryWord(text);
                if (!query_word.is_stop) {
                    if (query_word.is_minus) {
                        result.minus_words.push_back(query_word.data);
                    } else {
                        result.plus_words.push_back(query_word.data);
                    }
                }
            }
            break;
        }
        QueryWord query_word = ParseQueryWord(text.substr(0, space));
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
        text.remove_prefix(space + 1);
    }
    return result;
}

SearchServer::Query SearchServer::ParseQuery(string_view text) const {
    Query result = ParseQueryNoSort(text);
    sort(result.minus_words.begin(), result.minus_words.end());
    result.minus_words.resize(unique(
        result.minus_words.begin(), result.minus_words.end()
        ) - result.minus_words.begin());
    sort(result.plus_words.begin(), result.plus_words.end());
    result.plus_words.resize(unique(
        result.plus_words.begin(), result.plus_words.end()
        ) - result.plus_words.begin());
    return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(const string& word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}