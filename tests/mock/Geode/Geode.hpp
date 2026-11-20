// MOCK minimo de cocos2d-x / Geode, SOLO para compilar y probar src/ui/UiCollect.hpp sin el SDK (no se usa en el mod real).
// Las transformaciones son afines reales (posicion, ancla, escala; sin giro).
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
namespace cocos2d {
struct CCPoint { float x = 0, y = 0; };
struct CCSize { float width = 0, height = 0; };
struct CCRect { CCPoint origin; CCSize size; };
struct ccColor3B { unsigned char r, g, b; };
inline CCPoint ccp(float x, float y) { return {x, y}; }
class CCObject { public: virtual ~CCObject() = default; };
class CCArray : public CCObject {
public: std::vector<CCObject*> v; unsigned count() const { return unsigned(v.size()); } CCObject* objectAtIndex(unsigned i) { return v[i]; } };
class CCNode : public CCObject {
public:
    CCPoint pos, anchor{0.5f, 0.5f}; CCSize size; float sx = 1, sy = 1, rot = 0; int tag = -1, z = 0; bool vis = true; std::string id; CCNode* parent = nullptr; CCArray kids;
    void setPosition(CCPoint p) { pos = p; } void setContentSize(CCSize s) { size = s; } void setScale(float s) { sx = sy = s; } void setAnchorPoint(CCPoint a) { anchor = a; }
    void setID(std::string s) { id = std::move(s); } void setVisible(bool b) { vis = b; }
    CCPoint getPosition() const { return pos; } CCPoint getAnchorPoint() const { return anchor; } CCSize getContentSize() const { return size; }
    float getScaleX() const { return sx; } float getScaleY() const { return sy; } float getRotation() const { return rot; } float getSkewX() const { return 0; } float getSkewY() const { return 0; }
    int getTag() const { return tag; } int getZOrder() const { return z; } bool isVisible() const { return vis; } std::string getID() const { return id; }
    CCArray* getChildren() { return kids.count() ? &kids : nullptr; } unsigned getChildrenCount() const { return kids.count(); }
    void addChild(CCNode* c) { c->parent = this; kids.v.push_back(c); }
    CCPoint toParent(CCPoint l) const { return {pos.x + (l.x - anchor.x * size.width) * sx, pos.y + (l.y - anchor.y * size.height) * sy}; }
    CCPoint convertToWorldSpace(CCPoint l) const { CCPoint p = toParent(l); return parent ? parent->convertToWorldSpace(p) : p; }
    CCPoint worldToLocal(CCPoint w) const { CCPoint p = parent ? parent->worldToLocal(w) : w; return {(p.x - pos.x) / sx + anchor.x * size.width, (p.y - pos.y) / sy + anchor.y * size.height}; }
    CCPoint convertToNodeSpace(CCPoint w) const { return worldToLocal(w); }
};
class CCNodeRGBA : public CCNode { public: ccColor3B col{255, 255, 255}; unsigned char op = 255; ccColor3B getColor() const { return col; } unsigned char getOpacity() const { return op; } };
class CCSprite : public CCNodeRGBA { public: CCRect getTextureRect() const { return {{0, 0}, size}; } bool isFlipX() const { return false; } };
class CCLabelBMFont : public CCNodeRGBA { public: std::string str, fnt; const char* getString() const { return str.c_str(); } const char* getFntFile() const { return fnt.c_str(); } };
class CCLayerColor : public CCNodeRGBA {};
class CCMenu : public CCNode {};
class CCMenuItem : public CCNode { public: bool en = true; bool isEnabled() const { return en; } void setEnabled(bool b) { en = b; } bool isSelected() const { return false; } };
class CCMenuItemSpriteExtra : public CCMenuItem {};
class CCMenuItemToggler : public CCMenuItem { public: bool on = false; bool isToggled() const { return on; } };
class SliderThumb : public CCNode { public: float val = 0; float getValue() const { return val; } };
class CCEGLView { public: CCSize getFrameSize() const { return {1280, 720}; } CCSize getDesignResolutionSize() const { return {569, 320}; } float getScaleX() const { return 2.25f; } float getScaleY() const { return 2.25f; } };
class CCDirector { public: CCEGLView v; CCSize getWinSize() const { return {569, 320}; } CCSize getWinSizeInPixels() const { return {1280, 720}; } float getContentScaleFactor() const { return 4.f; } CCEGLView* getOpenGLView() { return &v; } static CCDirector* get() { static CCDirector d; return &d; } CCNode* scene = nullptr; CCNode* getRunningScene() { return scene; } };
class Slider : public CCNode { public: SliderThumb* th = nullptr; SliderThumb* getThumb() { return th; } };
}
namespace geode {
template <class T, class F> T typeinfo_cast(F* p) { return dynamic_cast<T>(p); }
namespace prelude { using namespace cocos2d; using geode::typeinfo_cast; }
}
