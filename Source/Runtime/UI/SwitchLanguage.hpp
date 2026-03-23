#pragma once
#include <string>
#include <unordered_map>

namespace engine {

    enum class Language {
        English,
        Chinese
    };

    // 改名为 Translator，避免任何撞名冲突！
    class Translator {
    public:
        static Language CurrentLanguage;

        static void SetLanguage(Language lang) {
            CurrentLanguage = lang;
        }

        static const char* SL(const char* key) {
            if (CurrentLanguage == Language::English) {
                return key;
            }

            if (CurrentLanguage == Language::Chinese) {
                auto it = ChineseDict.find(key);
                if (it != ChineseDict.end()) {
                    return it->second.c_str();
                }
            }
            return key;
        }

    private:
        static std::unordered_map<std::string, std::string> ChineseDict;
    };

} // namespace engine

//宏定义放在 namespace 外面，确保全局可用
#define _SL(key) engine::Translator::SL(key)