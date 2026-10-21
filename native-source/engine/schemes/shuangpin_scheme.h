#pragma once

#include "input_scheme.h"
#include "../shuangpin/shuangpin_profile.h"
#include <string>
#include <vector>

class ShuangpinScheme : public IInputScheme
{
  public:
    explicit ShuangpinScheme(const ShuangpinProfile &profile = GetXiaoheShuangpinProfile());
    void reset() override;
    void handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch) override;
    QueryRequest build_request() const override;
    std::string get_preedit() const override;
    SchemeType type() const override;
    void set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases) override;
    // 直接辅助码开着时收 /（四码后的终止键），; 韵母也不再要求所在一节是奇数键：辅码会打乱奇偶。
    void set_direct_helpcode(bool enabled)
    {
        direct_helpcode_ = enabled;
    }
    // 句中辅助码的大写触发：完整音节后的大写字母开一段（FanyImeMidSentenceHelpcode::StartsUppercaseBlock）。
    // 由会话按「句中辅助码开、勾了大写、没开直接辅助码」设置，原串照样记大写字母，只是解析规则变了。
    void set_mid_sentence_uppercase_trigger(bool enabled)
    {
        mid_sentence_uppercase_trigger_ = enabled;
    }
    // 末尾辅助码（间接）的大写触发：完整音节后的大写字母开一段末尾段。由会话按「双拼辅助码开、关着直接、
    // 勾了大写、句中辅助码没占着大写」设置。
    void set_end_helpcode_uppercase_trigger(bool enabled)
    {
        end_helpcode_uppercase_trigger_ = enabled;
    }

  private:
    const ShuangpinProfile profile_;
    bool direct_helpcode_ = false;
    bool mid_sentence_uppercase_trigger_ = false;
    bool end_helpcode_uppercase_trigger_ = false;
    std::string raw_input_;
    std::vector<KeyStroke> key_strokes_;
};
