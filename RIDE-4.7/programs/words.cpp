// words.cpp - split a sentence into words, sort them, count the longest.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    std::string text = "the quick brown fox jumps over the lazy dog";
    std::vector<std::string> words;
    std::string word;
    for (std::size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || text[i] == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += text[i];
        }
    }
    std::sort(words.begin(), words.end());
    std::size_t longest = 0;
    for (std::size_t i = 0; i < words.size(); i++) {
        std::printf("%s ", words[i].c_str());
        if (words[i].size() > longest) longest = words[i].size();
    }
    std::printf("\n%d words, the longest %d letters\n", (int)words.size(), (int)longest);
    return 0;
}
