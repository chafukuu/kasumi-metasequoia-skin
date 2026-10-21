#pragma once

#include "../core/word_item.h"

#include <string>
#include <vector>

namespace metasequoia::local_modes
{
// V 模式的候选，input 是 V（或全拼的 v）后面那一串，形状见 contracts/v_mode_input.h。
//
// 纯数字转中文：
//   123      一百二十三、壹佰贰拾叁、壹佰贰拾叁元整、一二三
//   12345    再加一条千分位 12,345
//   123.45   壹佰贰拾叁元肆角伍分、一百二十三点四五、壹佰贰拾叁点肆伍（超过两位小数不出金额）
// 整数部分超过 16 位（万亿以上）时不出读法，只留逐位的那条。
//
// 含运算符的是算式，支持 + - * / 和括号、一元负号：
//   1+2*3    7、1+2*3=7
// 除以零、写错或没写完时没有候选。
std::vector<WordItem> query_v_mode(const std::string &input, int limit = 10);

// 下面几个单独给出来供测试和别处复用。整数部分是十进制数字串，可以带前导零。
// 读法：一百二十三、十五、一万零一十；超过 16 位返回空串。
std::string chinese_number_reading(const std::string &digits);
// 大写：壹佰贰拾叁、壹拾伍；超过 16 位返回空串。
std::string chinese_financial_number(const std::string &digits);
// 金额大写：整数部分加至多两位小数，壹佰贰拾叁元肆角伍分；超出范围返回空串。
std::string chinese_money(const std::string &integer_digits, const std::string &fraction_digits);
// 算式求值的结果文本，不是合法算式时返回 false。
bool evaluate_v_mode_expression(const std::string &expression, std::string &result);
} // namespace metasequoia::local_modes
