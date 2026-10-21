#include "settings/native/native_page_voice_fields.h"

#include "ai/jev_client.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Scene.h"
#include "msimeui/Window.h"

#include <algorithm>
#include <map>

// partials/ai-settings.html + modules/ai-settings.ts, ai-assistant-state.ts,
// model-fetch.ts and credential-test.ts
namespace native_settings
{
using namespace voice_fields;

namespace
{
struct ProviderDefaults
{
    const char *endpoint;
    const char *model;
};

const std::map<std::string, ProviderDefaults> &Defaults()
{
    static const std::map<std::string, ProviderDefaults> defaults = {
        {"deepseek", {"https://api.deepseek.com/chat/completions", "deepseek-v4-flash"}},
        {"openai", {"https://api.openai.com/v1/chat/completions", "gpt-4o-mini"}},
        {"siliconflow", {"https://api.siliconflow.cn/v1/chat/completions", "Qwen/Qwen3-8B"}},
        {"groq", {"https://api.groq.com/openai/v1/chat/completions", "qwen/qwen3.8-27b"}},
        {"custom", {"", ""}},
        {"jev", {Jev::kEndpoint, Jev::kModel}},
    };
    return defaults;
}

const std::vector<std::string> kProviders = {"deepseek", "openai", "siliconflow", "groq", "custom", "jev"};

ProviderDefaults DefaultsOf(const std::string &provider)
{
    const auto it = Defaults().find(provider);
    return it != Defaults().end() ? it->second : ProviderDefaults{"", ""};
}

// ---------------------------------------------------------------------------------
// Model combobox: `.dropdown-toggle.font-combobox.ai-model-combobox` with the model
// list (`#aiModelMenu.font-dropdown-menu.compact-scroll-menu`) that 查询模型 fills.
// ---------------------------------------------------------------------------------
class ModelCombo;

class ModelList : public msimeui::Visual
{
  public:
    static constexpr float kItem = 28.0f;
    static constexpr float kGap = 2.0f;
    static constexpr float kInset = 2.0f;

    explicit ModelList(ModelCombo *owner) : owner_(owner)
    {
    }

    void SetItems(std::vector<std::wstring> items)
    {
        items_ = std::move(items);
        scroll_ = 0.0f;
        hovered_ = kNone;
        InvalidateMeasure();
    }

    bool Empty() const
    {
        return items_.empty();
    }

    float ContentHeight() const
    {
        const float rows = static_cast<float>(items_.size());
        return kInset * 2.0f + rows * kItem + std::max(rows - 1.0f, 0.0f) * kGap;
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        // 6 rows visible (border box 182px).
        const float visible = 6.0f * 28.0f + 5.0f * 2.0f + 2.0f * 2.0f - 2.0f;
        // No intrinsic width: the popup matches the combobox (max of this and the anchor).
        return {0.0f, std::min(ContentHeight(), visible)};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
        Clamp();
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        const Palette &p = CurrentPalette();
        ID2D1RenderTarget *target = dr.GetRenderTarget();
        target->PushAxisAlignedClip(ToD2D(bounds_), D2D1_ANTIALIAS_MODE_ALIASED);
        for (size_t i = 0; i < items_.size(); ++i)
        {
            const RectF item = ItemRect(i);
            if (item.y + item.height < bounds_.y || item.y > bounds_.y + bounds_.height)
                continue;
            if (i == hovered_)
                FillRounded(dr, item, 8.0f, p.dropdownItemHover);
            // white-space: nowrap; text-overflow: ellipsis
            TextLayout text;
            TextStyle style = BodyStyle();
            text.Set(items_[i], style);
            const float width = item.width - 16.0f;
            IDWriteTextLayout *layout = text.Get(width);
            if (layout)
            {
                layout->SetMaxWidth(width);
                DWRITE_TRIMMING trimming = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
                DWriteFactory()->CreateEllipsisTrimmingSign(layout, ellipsis.GetAddressOf());
                layout->SetTrimming(&trimming, ellipsis.Get());
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                if (ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(i == hovered_ ? p.text : p.dropdownItem))
                    target->DrawTextLayout(D2D1::Point2F(item.x + 8.0f, item.y + (item.height - metrics.height) * 0.5f),
                                           layout, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
        target->PopAxisAlignedClip();
        const float overflow = ContentHeight() - bounds_.height;
        if (overflow > 0.5f)
        {
            const float track = bounds_.height - 8.0f;
            const float thumb = std::max(24.0f, track * bounds_.height / ContentHeight());
            FillRounded(dr,
                        {bounds_.x + bounds_.width - 6.0f, bounds_.y + 4.0f + (track - thumb) * (scroll_ / overflow),
                         4.0f, thumb},
                        2.0f, p.scrollbarThumb);
        }
    }

    bool OnMouseMove(const POINT &point, WPARAM) override
    {
        const size_t hit = window_ ? HitRow(window_->ClientPixelsToDips(point)) : kNone;
        if (hit != hovered_)
        {
            hovered_ = hit;
            InvalidateVisual();
        }
        return true;
    }

    void OnMouseLeave() override
    {
        hovered_ = kNone;
        InvalidateVisual();
    }

    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = window_ ? HitRow(window_->ClientPixelsToDips(point)) : kNone;
        return true;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override;

    bool OnMouseWheel(const POINT &, short delta, WPARAM) override
    {
        scroll_ -= static_cast<float>(delta) / WHEEL_DELTA * (kItem + kGap) * 2.0f;
        Clamp();
        InvalidateVisual();
        return true;
    }

  private:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    RectF ItemRect(size_t i) const
    {
        return {bounds_.x + kInset, bounds_.y + kInset + static_cast<float>(i) * (kItem + kGap) - scroll_,
                bounds_.width - kInset * 2.0f, kItem};
    }

    size_t HitRow(const PointF &point) const
    {
        if (!Contains(bounds_, point))
            return kNone;
        for (size_t i = 0; i < items_.size(); ++i)
        {
            if (Contains(ItemRect(i), point))
                return i;
        }
        return kNone;
    }

    void Clamp()
    {
        scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, ContentHeight() - bounds_.height));
    }

    ModelCombo *owner_ = nullptr;
    std::vector<std::wstring> items_;
    size_t hovered_ = kNone;
    size_t pressed_ = kNone;
    float scroll_ = 0.0f;
};

class ModelPopup : public msimeui::Popup
{
  public:
    explicit ModelPopup(const std::shared_ptr<ModelList> &list) : msimeui::Popup(list), list_(list.get())
    {
        SetPadding(1.0f);
        SetPlacement(msimeui::PopupPlacement::BelowLeading);
        SetCornerRadius(10.0f);
        SetShadowEnabled(true);
        SetConstrainToViewport(true);
        SetMatchAnchorWidth(true);
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        ScopedTextAntialias grayscale(dr, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        msimeui::Popup::Render(dr);
    }

    bool OnMouseWheel(const POINT &point, short delta, WPARAM keyState) override
    {
        if (!window_ || !HitTest(window_->ClientPixelsToDips(point)))
            return false;
        list_->OnMouseWheel(point, delta, keyState);
        return true;
    }

  private:
    ModelList *list_ = nullptr;
};

class ModelCombo : public msimeui::Visual
{
  public:
    using Handler = std::function<void(const std::wstring &)>;

    ModelCombo()
    {
        InputBox::Options options;
        options.bare = true;
        options.font = FieldTextStyle(kFontBody);
        input_ = std::make_shared<InputBox>(options);
        AdoptChild(input_);
        list_ = std::make_shared<ModelList>(this);
        popup_ = std::make_shared<ModelPopup>(list_);
    }

    std::shared_ptr<InputBox> Input() const
    {
        return input_;
    }

    void SetOnPicked(Handler handler)
    {
        onPicked_ = std::move(handler);
    }

    // Arrow click with no list yet: same as 查询模型.
    void SetOnFetch(std::function<void()> handler)
    {
        onFetch_ = std::move(handler);
    }

    void SetModels(std::vector<std::wstring> models)
    {
        Close();
        list_->SetItems(std::move(models));
    }

    bool HasModels() const
    {
        return !list_->Empty();
    }

    void Open()
    {
        msimeui::Scene *scene = window_ ? window_->GetScene() : nullptr;
        if (open_ || !scene || list_->Empty())
            return;
        scene->ClearPopups(true);
        const Palette &p = CurrentPalette();
        popup_->SetBackgroundFill(p.dropdownMenuBg);
        popup_->SetBorderColor(p.dropdownMenuBorder);
        popup_->SetTopHighlight(p.dropdownMenuHighlight);
        popup_->SetShadowPasses({{p.menuShadow[0].sigma, p.menuShadow[0].alpha, 0.0f, p.menuShadow[0].offsetY},
                                 {p.menuShadow[1].sigma, p.menuShadow[1].alpha, 0.0f, p.menuShadow[1].offsetY}});
        popup_->SetAnchorRect(bounds_);
        popup_->SetOffset(0.0f, 2.0f);
        scene->AddPopup(popup_, [this]() {
            open_ = false;
            InvalidateVisual();
        });
        popup_->BeginAppearAnimation(140.0, -3.0f, 0.985f);
        open_ = true;
        window_->Invalidate();
    }

    void Close()
    {
        if (!open_)
            return;
        if (msimeui::Scene *scene = window_ ? window_->GetScene() : nullptr)
            scene->RemovePopup(popup_.get(), false);
        open_ = false;
        InvalidateVisual();
    }

    void Pick(const std::wstring &model)
    {
        Close();
        input_->SetText(model);
        if (onPicked_)
            onPicked_(model);
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        input_->MeasureInLayout({std::max(availableSize.width - 12.0f - 6.0f - 18.0f - 2.0f, 0.0f), 40.0f});
        return {availableSize.width, 40.0f};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
        popup_->SetAnchorRect(bounds_);
        const float border = CssBorder();
        const float lineHeight = LineHeight(FieldTextStyle(kFontBody)) + 2.0f;
        input_->ArrangeInLayout({finalRect.x + border + 12.0f, finalRect.y + (finalRect.height - lineHeight) * 0.5f,
                                 std::max(finalRect.width - border * 2.0f - 12.0f - 6.0f - 18.0f, 0.0f), lineHeight});
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        const Palette &p = CurrentPalette();
        const bool focused = input_->IsFocused();
        // :focus-within ring and border, the dropdown look otherwise.
        if (focused || open_)
            FillRounded(dr, Inflate(bounds_, 2.0f), 10.0f, p.accentSoft);
        FillRounded(dr, bounds_, 8.0f, hovered_ || open_ ? p.surfaceRaised : p.dropdownBg);
        StrokeRounded(dr, bounds_, 8.0f, focused || open_ ? p.accentSoftBorder : (hovered_ ? p.borderStrong : p.border),
                      1.0f);
        input_->Render(dr);
        static const std::string arrow = LoadAssetSvg(L"arrow.svg");
        static const std::string lightArrow = InvertSvgColors(arrow, 0.25f);
        const RectF a = ArrowRect();
        DrawSvg(dr, p.light ? lightArrow : arrow, a);
    }

    void Attach(msimeui::Window *window) override
    {
        msimeui::Visual::Attach(window);
        input_->Attach(window);
        popup_->Attach(window);
    }

    msimeui::Visual *FindVisualAt(const PointF &point) override
    {
        if (!HitTest(point))
            return nullptr;
        if (Contains(ArrowRect(), point))
            return this;
        return input_->FindVisualAt(point) ? input_->FindVisualAt(point) : this;
    }

    msimeui::Visual *FindFocusableAt(const PointF &point) override
    {
        if (!HitTest(point) || Contains(ArrowRect(), point))
            return nullptr;
        return input_->FindFocusableAt(point);
    }

    bool KeepsPopupsOpenOnClick() const override
    {
        return true;
    }

    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = window_ && HitTest(window_->ClientPixelsToDips(point));
        return pressed_;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
    {
        if (!pressed_ || !window_)
            return false;
        pressed_ = false;
        const PointF dip = window_->ClientPixelsToDips(point);
        if (!HitTest(dip))
            return true;
        if (Contains(ArrowRect(), dip) && list_->Empty())
        {
            if (onFetch_)
                onFetch_();
            return true;
        }
        open_ ? Close() : Open();
        return true;
    }

    void OnMouseEnter() override
    {
        hovered_ = true;
        InvalidateVisual();
    }

    void OnMouseLeave() override
    {
        hovered_ = false;
        InvalidateVisual();
    }

  private:
    RectF ArrowRect() const
    {
        return {bounds_.x + bounds_.width - CssBorder() - 6.0f - 18.0f, bounds_.y + (bounds_.height - 18.0f) * 0.5f,
                18.0f, 18.0f};
    }

    std::shared_ptr<InputBox> input_;
    std::shared_ptr<ModelList> list_;
    std::shared_ptr<ModelPopup> popup_;
    Handler onPicked_;
    std::function<void()> onFetch_;
    bool hovered_ = false;
    bool pressed_ = false;
    bool open_ = false;
};

bool ModelList::OnMouseUp(const POINT &point, WPARAM)
{
    const size_t hit = window_ ? HitRow(window_->ClientPixelsToDips(point)) : kNone;
    if (hit != kNone && hit == pressed_)
        owner_->Pick(items_[hit]);
    pressed_ = kNone;
    return true;
}

// An input in the 40px `.ai-grid` look.
std::shared_ptr<InputBox> GridInput(bool token = false)
{
    InputBox::Options options;
    options.height = 40.0f;
    if (token)
    {
        options.font = MonospaceStyle();
        options.letterSpacing = MonospaceTokenSpacing();
        options.password = true;
        options.reveal = true;
    }
    return std::make_shared<InputBox>(options);
}

std::shared_ptr<Dropdown> GridDropdown(std::vector<DropdownOption> options)
{
    // `.ai-grid .dropdown-toggle`: full width and 40px tall; the menu stays right-aligned
    // and shrink-to-fit.
    Dropdown::Options settings;
    // Inside an `.ai-field`, whose font-weight: 500 the button and menu inherit.
    settings.weight = DWRITE_FONT_WEIGHT_MEDIUM;
    auto dropdown = std::make_shared<Dropdown>(std::move(options), settings);
    dropdown->SetHeight(40.0f);
    return dropdown;
}

struct AiPage : std::enable_shared_from_this<AiPage>
{
    explicit AiPage(PageContext &context) : context(context)
    {
    }

    PageContext &context;
    int localWrites = 0;
    std::string provider = "deepseek";
    std::map<std::string, std::string> tokens;
    std::map<std::string, std::string> endpoints;
    std::map<std::string, std::string> models;
    std::string promptId = "custom_1";
    std::map<std::string, std::string> customPrompts = {{"custom_1", ""}, {"custom_2", ""}, {"custom_3", ""}};

    std::shared_ptr<Dropdown> providerDropdown;
    std::shared_ptr<ModelCombo> model;
    std::shared_ptr<InputBox> token;
    std::shared_ptr<InputBox> limit;
    std::shared_ptr<InputBox> endpoint;
    std::shared_ptr<Hideable> thinkingField;
    std::shared_ptr<Dropdown> promptSlot;
    std::shared_ptr<InputBox> prompt;
    std::shared_ptr<PlainButton> fetchButton;
    std::shared_ptr<StatusText> fetchStatus;
    std::unique_ptr<CredentialTest> test;
    std::string pendingFetch;

    void Write(const std::string &path, Json value)
    {
        ++localWrites;
        context.model.Update(path, std::move(value));
        --localWrites;
    }

    // Credential test and model list share one request config; empty values fall back to
    // the provider defaults.
    Json CurrentConfig() const
    {
        const ProviderDefaults defaults = DefaultsOf(provider);
        const std::string endpointValue = Narrow(Trim(endpoint->GetText()));
        const std::string modelValue = Narrow(Trim(model->Input()->GetText()));
        return Json{{"provider", provider},
                    {"token", Narrow(Trim(token->GetText()))},
                    {"endpoint", !endpointValue.empty() ? endpointValue : std::string(defaults.endpoint)},
                    {"model", !modelValue.empty() ? modelValue : std::string(defaults.model)}};
    }

    void ApplyProviderFields()
    {
        const ProviderDefaults defaults = DefaultsOf(provider);
        endpoint->SetText(
            Widen(provider != "jev" && !endpoints[provider].empty() ? endpoints[provider] : defaults.endpoint));
        model->Input()->SetText(Widen(!models[provider].empty() ? models[provider] : defaults.model));
        model->Input()->SetPlaceholder(Widen(defaults.model));
    }

    void ClearModelMenu()
    {
        pendingFetch.clear();
        fetchButton->SetDisabled(false);
        fetchButton->SetText(L"查询模型");
        model->SetModels({});
        fetchStatus->Clear();
    }

    void SwitchProvider(const std::string &next)
    {
        tokens[provider] = Narrow(Trim(token->GetText()));
        Write("ai_assistant.token_" + provider, tokens[provider]);
        endpoints[provider] = Narrow(endpoint->GetText());
        Write("ai_assistant.endpoint", endpoints[provider]);
        models[provider] = Narrow(model->Input()->GetText());
        Write("ai_assistant.model", models[provider]);
        provider = next;
        token->SetText(Widen(tokens[provider]));
        Write("ai_assistant.provider", provider);
        ApplyProviderFields();
        thinkingField->SetVisible(provider == "deepseek");
        ClearModelMenu();
    }

    void SwitchPrompt(const std::string &id)
    {
        customPrompts[promptId] = Narrow(prompt->GetText());
        Write("ai_assistant.prompt_" + promptId, customPrompts[promptId]);
        promptId = id;
        prompt->SetText(Widen(customPrompts[id]));
    }

    void Fetch()
    {
        if (!pendingFetch.empty())
            return;
        pendingFetch = context.model.NewRequestId("models");
        fetchButton->SetDisabled(true);
        fetchButton->SetText(L"获取中…");
        fetchStatus->Set(L"正在向服务商查询模型列表…", "pending");
        context.model.Post("apiModelList",
                           Json{{"requestId", pendingFetch}, {"service", "ai.assistant"}, {"config", CurrentConfig()}});
    }

    void OnModels(const Json &message)
    {
        if (pendingFetch.empty() || message.value("requestId", std::string()) != pendingFetch)
            return;
        pendingFetch.clear();
        fetchButton->SetDisabled(false);
        fetchButton->SetText(L"查询模型");
        if (!message.value("ok", false))
        {
            fetchStatus->Set(Widen(message.value("message", std::string())), "error");
            return;
        }
        std::vector<std::wstring> names;
        if (message.contains("models") && message["models"].is_array())
        {
            for (const auto &item : message["models"])
            {
                if (item.is_string() && !Narrow(Trim(Widen(item.get<std::string>()))).empty())
                    names.push_back(Widen(item.get<std::string>()));
            }
        }
        model->SetModels(names);
        if (names.empty())
        {
            const std::string text = message.value("message", std::string());
            fetchStatus->Set(text.empty() ? L"服务商没有返回可用模型" : Widen(text), "error");
            return;
        }
        fetchStatus->Set(L"获取到 " + std::to_wstring(names.size()) + L" 个模型", "success");
        model->Open();
    }

    // applyAiConfig
    void Sync()
    {
        if (localWrites > 0)
            return;
        const SettingsModel &m = context.model;
        provider = m.GetString("ai_assistant.provider", "deepseek");
        endpoint->SetText(Widen(m.GetString("ai_assistant.endpoint")));
        model->Input()->SetText(Widen(m.GetString("ai_assistant.model")));
        if (const Json *value = m.Find("ai_assistant.candidate_limit"); value && value->is_number())
            limit->SetText(std::to_wstring(value->get<int64_t>()));
        tokens = ReadProviderMap(m, "ai_assistant", "token", provider, kProviders, true);
        endpoints = ReadProviderMap(m, "ai_assistant", "endpoint", provider, kProviders, false);
        models = ReadProviderMap(m, "ai_assistant", "model", provider, kProviders, false);
        token->SetText(Widen(tokens[provider]));
        providerDropdown->SetValue(provider);
        thinkingField->SetVisible(provider == "deepseek");
        model->Input()->SetPlaceholder(Widen(DefaultsOf(provider).model));
        promptId = m.GetString("ai_assistant.prompt_id", "custom_1");
        const Json *custom1 = m.Find("ai_assistant.prompt_custom_1");
        customPrompts["custom_1"] =
            custom1 && custom1->is_string() ? custom1->get<std::string>() : m.GetString("ai_assistant.prompt");
        customPrompts["custom_2"] = m.GetString("ai_assistant.prompt_custom_2");
        customPrompts["custom_3"] = m.GetString("ai_assistant.prompt_custom_3");
        promptSlot->SetValue(promptId);
        prompt->SetText(Widen(customPrompts[promptId]));
    }

    VisualPtr Build()
    {
        PageBuilder page(L"AI 与云联想");
        std::weak_ptr<AiPage> weak = shared_from_this();

        page.AddSection(MakeRows({MakeRow(L"启用 AI 联想", L"异步生成候选；Jev 从已有候选中选择并标记推荐项",
                                          BindToggle(context, "ai_assistant.enabled")),
                                  MakeRow(L"启用云联想", L"", BindToggle(context, "general.cloud_candidates"))}));

        // API 配置
        {
            auto column = std::make_shared<Column>(0.0f);
            auto heading = MakeTitle(L"API 配置");
            heading->SetMargin({0.0f, 0.0f, 0.0f, 18.0f});
            column->Add(heading);
            auto description = MakeDescription(
                L"DeepSeek、OpenAI、SiliconFlow、Groq 均使用 OpenAI 兼容的 Chat Completions 接口。Custom "
                L"用于接入兼容服务。Jev 使用 TypeSafe 官方 Choice 接口，将前文、拼音和最多 32 个本地候选发送给服务商。"
                L"Jev 固定选择一个候选，不使用下方的候选数量、思考强度和自定义提示词。各提供商凭据分开保存。");
            description->SetMargin({0.0f, -10.0f, 0.0f, 16.0f});
            column->Add(description);

            auto grid = std::make_shared<FormGrid>();
            providerDropdown = GridDropdown(Options({{"deepseek", L"DeepSeek"},
                                                     {"openai", L"OpenAI"},
                                                     {"siliconflow", L"SiliconFlow"},
                                                     {"groq", L"Groq"},
                                                     {"custom", L"Custom"},
                                                     {"jev", L"Jev (TypeSafe)"}}));
            providerDropdown->SetOnChanged(
                [this](size_t, const DropdownOption &option) { SwitchProvider(option.value); });
            grid->Add(std::make_shared<Field>(L"服务提供商", providerDropdown));

            model = std::make_shared<ModelCombo>();
            model->SetOnPicked([this](const std::wstring &value) { Write("ai_assistant.model", Narrow(value)); });
            model->SetOnFetch([this]() { Fetch(); });
            model->Input()->SetOnCommit([this](const std::wstring &text) {
                Write("ai_assistant.model", Narrow(text));
                if (text.empty())
                    model->Input()->SetText(Widen(DefaultsOf(provider).model));
            });
            fetchButton = std::make_shared<PlainButton>(L"查询模型", ModelFetchButtonStyle());
            fetchButton->SetOnClick([this]() { Fetch(); });
            auto modelRow = std::make_shared<Row>(8.0f);
            modelRow->SetAlign(Row::Align::Stretch);
            modelRow->Add(model, 1.0f);
            modelRow->Add(fetchButton);
            auto testButton = std::make_shared<PlainButton>(L"测试连接", CredentialButtonStyle());
            auto testStatus = std::make_shared<StatusText>();
            auto actions = std::make_shared<Row>(10.0f);
            actions->Add(testButton);
            actions->Add(testStatus, 1.0f);
            fetchStatus = std::make_shared<StatusText>();
            auto modelField = std::make_shared<Field>(L"模型", modelRow);
            // `.ai-model-fetch-status:empty { display: none }`: no flex gap while empty, so
            // the 7px gap rides on the status as a margin a hidden Hideable drops.
            auto tail = std::make_shared<Column>(0.0f);
            tail->Add(std::make_shared<MinHeight>(actions, 20.0f));
            fetchStatus->SetMargin({0.0f, 7.0f, 0.0f, 0.0f});
            tail->Add(fetchStatus);
            modelField->Add(tail);
            grid->Add(modelField);
            test = std::make_unique<CredentialTest>(
                context, testButton, testStatus, []() { return std::string("ai.assistant"); },
                [this]() { return CurrentConfig(); });
            context.OnMessage("apiModelListResult", [weak](const Json &message) {
                if (auto page = weak.lock())
                    page->OnModels(message);
            });

            token = GridInput(true);
            token->SetOnCommit([this](const std::wstring &text) {
                tokens[provider] = Narrow(Trim(text));
                Write("ai_assistant.token_" + provider, tokens[provider]);
            });
            grid->Add(std::make_shared<Field>(L"API Token", token));

            limit = GridInput();
            limit->SetOnCommit([this](const std::wstring &text) {
                int value = 3;
                try
                {
                    value = std::stoi(Narrow(Trim(text)));
                }
                catch (...)
                {
                    value = 3;
                }
                if (value == 0)
                    value = 3;
                value = std::clamp(value, 1, 10);
                limit->SetText(std::to_wstring(value));
                Write("ai_assistant.candidate_limit", value);
            });
            grid->Add(std::make_shared<Field>(L"候选数量", limit));

            auto thinking =
                BindDropdown(context, "ai_assistant.thinking",
                             Options({{"disabled", L"关闭"}, {"low", L"低"}, {"high", L"高"}, {"max", L"最高"}}));
            thinking->SetHeight(40.0f);
            auto thinkingColumn = std::make_shared<Field>(L"思考强度", thinking);
            thinkingColumn->Add(MakeDescription(L"开启后候选更准，但要多等几秒到几十秒。"));
            thinkingField = std::make_shared<Hideable>(thinkingColumn);
            grid->Add(thinkingField);

            endpoint = GridInput();
            endpoint->SetOnCommit([this](const std::wstring &text) {
                // Jev only talks to the official endpoint; any other value would silently disable it.
                if (provider == "jev")
                {
                    Write("ai_assistant.endpoint", std::string(Jev::kEndpoint));
                    endpoint->SetText(Widen(Jev::kEndpoint));
                    return;
                }
                Write("ai_assistant.endpoint", Narrow(text));
                if (text.empty())
                    endpoint->SetText(Widen(DefaultsOf(provider).endpoint));
            });
            grid->Add(std::make_shared<Field>(L"接口地址", endpoint), true);
            column->Add(grid);
            page.AddSection(column);
        }

        // 自定义提示词
        {
            auto column = std::make_shared<Column>(0.0f);
            Dropdown::Options slotOptions;
            slotOptions.fixedWidth = 168.0f;
            slotOptions.weight = DWRITE_FONT_WEIGHT_MEDIUM;
            promptSlot = std::make_shared<Dropdown>(
                Options({{"custom_1", L"自定义一"}, {"custom_2", L"自定义二"}, {"custom_3", L"自定义三"}}),
                slotOptions);
            promptSlot->SetOnChanged([this](size_t, const DropdownOption &option) {
                Write("ai_assistant.prompt_id", option.value);
                SwitchPrompt(option.value);
            });
            auto header = std::make_shared<Field>(L"自定义提示词", promptSlot);
            header->SetWidth(168.0f);
            header->SetHorizontalAlignment(msimeui::HorizontalAlignment::Leading);
            header->SetMargin({0.0f, 0.0f, 0.0f, 16.0f});
            column->Add(header);
            InputBox::Options promptOptions;
            TextStyle font = BodyStyle();
            font.size = kFontDefault;
            promptOptions.font = font;
            promptOptions.multiline = true;
            promptOptions.height = 280.0f;
            // `.ai-prompt`: line-height 1.65, resize: none, the page's 8px scrollbar.
            promptOptions.lineHeight = 1.65f;
            promptOptions.scrollbar = InputBox::Options::Scrollbar::Thin;
            prompt = std::make_shared<InputBox>(promptOptions);
            // The <textarea> is inline-block: its line box adds the strut's descent below it.
            prompt->SetMargin({0.0f, 0.0f, 0.0f, 4.5f});
            prompt->SetOnCommit([this](const std::wstring &text) {
                customPrompts[promptId] = Narrow(text);
                Write("ai_assistant.prompt_" + promptId, customPrompts[promptId]);
            });
            column->Add(prompt);
            page.AddSection(column);
        }

        thinkingField->SetVisible(true);
        context.OnConfig([weak]() {
            if (auto page = weak.lock())
                page->Sync();
        });
        return page.Build();
    }
};

std::vector<std::shared_ptr<AiPage>> &KeepAlive()
{
    static std::vector<std::shared_ptr<AiPage>> pages;
    return pages;
}
} // namespace

VisualPtr BuildAiPage(PageContext &context)
{
    auto page = std::make_shared<AiPage>(context);
    KeepAlive().push_back(page);
    return page->Build();
}
} // namespace native_settings
