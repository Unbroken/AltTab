#pragma once

#include <string>

struct FuzzyMatchResult {
    double score;
    int start_pos;
    int end_pos;
};

double ratio(const std::wstring& s1, const std::wstring& s2);

FuzzyMatchResult partial_ratio(const std::wstring& s1, const std::wstring& s2);
