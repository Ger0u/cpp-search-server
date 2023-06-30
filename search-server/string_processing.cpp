#include "string_processing.h"
#include <functional>
#include <algorithm>
#include <iostream>

using namespace std;

vector<string_view> SplitIntoWords(string_view text) {
    vector<string_view> words;
    size_t space;
    while (true) {
        space = text.find(' ');
        if (space >= string_view::npos) {
            words.push_back(text);
            break;
        }
        words.push_back(text.substr(0, space));
        text.remove_prefix(space + 1);
    }
    return words;
}