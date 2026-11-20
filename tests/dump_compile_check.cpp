// Comprobacion de compilacion del bloque de volcado de UI contra stubs (NO es un test de comportamiento de Geode).
#include "Geode/Geode.hpp"
#include "UiCollect.hpp"
#include "gifanim.hpp"
#include "core.hpp"
#include <sstream>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cctype>
#include <memory>
#include <map>
#include <iostream>
using namespace geode::prelude;
// ---- stubs de Geode ----
struct Mod { std::map<std::string, std::string> v; static Mod* get() { static Mod m; return &m; }
    template <class T> T getSettingValue(std::string const& k) { auto it = v.find(k); if constexpr (std::is_same_v<T, bool>) return it != v.end() && it->second == "1"; else if constexpr (std::is_same_v<T, std::string>) return it == v.end() ? "" : it->second; else return T(0); }
    std::filesystem::path getSaveDir() { return "/tmp/uidump_save"; } };
enum class NotificationIcon { Success, Error, Info };
struct Notification { static Notification* create(std::string const& s, NotificationIcon, float = 1.f) { std::cout << "[Notification] " << s << "\n"; return new Notification(); } void show() {} };
namespace geode::log { template <class... A> void info(const char*, A&&...) {} }
namespace geode::prelude { namespace log = geode::log; }
namespace geode::utils::clipboard { inline bool write(std::string const& s) { std::cout << "[clipboard " << s.size() << " bytes]\n"; return true; } }
namespace utils = geode::utils;
using SEL_SCHEDULE = void (CCObject::*)(float);
#define schedule_selector(x) (void)0
namespace {
    constexpr char const* kModVersion = "v0.5.11-beta"; constexpr float kPopupW = 430.f, kPopupH = 290.f, kPrevSize = 140.f;
#include "/tmp/helpers.inc"
}
struct Popup : CCNode {};
class GifPopup : public Popup {
public:
    CCNode* m_mainLayer = nullptr; CCLabelBMFont *m_methodLabel = nullptr, *m_descLabel = nullptr, *m_statA = nullptr, *m_statB = nullptr, *m_noteLabel = nullptr, *m_perFrameValue = nullptr, *m_sizeValue = nullptr, *m_framesValue = nullptr;
    Slider *m_perFrameSlider = nullptr, *m_sizeSlider = nullptr, *m_framesSlider = nullptr; CCMenuItemSpriteExtra* m_dumpBtn = nullptr; CCSprite* m_preview = nullptr;
    std::shared_ptr<const gdx::GifData> m_gif; gdx::Result m_result; bool m_hasResult = false; int m_method = 2, m_perFrame = 300, m_sizeU = 600, m_maxFrames = 40; bool m_gray = false, m_chan = false;
    struct J {}; std::shared_ptr<J> m_job, m_build; std::string m_dumpReason;
    int paletteSize() const { return 128; }
    void unschedule(int) {} void scheduleOnce(int, float) {}
    void settingsPublic() {}
    std::string settingsText() const; std::string uiStateText() const; void refreshDumpButton(); void scheduleAutoDump(char const*, float); void autoDump(float); void onDumpUi(CCObject*); void dumpUi(std::string const&, bool);
};
#undef schedule_selector
#define schedule_selector(x) 0
#include "/tmp/block.inc"
int main() {
    auto* g = new GifPopup(); g->m_mainLayer = new CCNode(); g->m_mainLayer->setContentSize({430, 290}); g->m_mainLayer->setAnchorPoint({0.5f, 0.5f}); g->m_mainLayer->setPosition({284.5f, 160}); g->addChild(g->m_mainLayer);
    g->setContentSize({569, 320}); g->setAnchorPoint({0, 0});
    auto* l = new CCLabelBMFont(); l->str = "hola"; l->setContentSize({40, 12}); l->setPosition({100, 100}); g->m_mainLayer->addChild(l); g->m_statA = l;
    CCDirector::get()->scene = g;   // la escena del mock es el propio popup
    Mod::get()->v["ui-dump-scene"] = "1"; Mod::get()->v["ui-dump"] = "1";
    g->dumpUi("manual test!", true);
    std::cout << "OK compilado y ejecutado\n";
}
