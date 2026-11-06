#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "engine.hpp"
#include "gdexport.hpp"
#include "gifanim.hpp"
#include "layered.hpp"      // metodo 18 (capas: fondo + rests + parches)

using namespace geode::prelude;

// Ventana del importador de GIF (430x290):
//   izquierda: vista previa del frame 0 convertido + estado
//   derecha: metodo, objetos por frame, lado largo, frames maximos, gris
class GifPopup : public geode::Popup {
public:
    static GifPopup* create(EditorUI* editor);
    ~GifPopup();

protected:
    // Calculo del frame 0 (vista previa) en un hilo aparte
    struct PreviewJob {
        gdx::Progress prog;
        std::shared_ptr<const gdx::GifData> gif;
        int frameIdx = 0;
        gdx::Method method = gdx::Method::BSP;
        gdx::Options opt;
        int id = 0;
        gdx::Result result;
        gdx::GifPlan plan;                                   // metodo 18
        std::shared_ptr<gdx::LayeredAnim> layered;           // metodo 18: capas completas (el result es el frame 0 montado)
        std::atomic<bool> done{false};
    };
    // Conversion de todos los frames (al pulsar Colocar)
    struct BuildJob {
        gdx::Progress prog;
        std::shared_ptr<const gdx::GifData> gif;
        gdx::GifPlan plan;
        gdx::Method method = gdx::Method::BSP;
        gdx::Options opt;
        int threads = 2;
        std::vector<gdx::Result> results;
        std::shared_ptr<gdx::LayeredAnim> layered;           // metodo 18
        std::atomic<bool> done{false};
    };

    bool init(EditorUI* editor);

    void onPick(CCObject*);
    void onPlace(CCObject*);
    void onInfo(CCObject*);
    void onSettings(CCObject*);
    void onPrev(CCObject*) { stepMethod(-1); }
    void onNext(CCObject*) { stepMethod(+1); }
    void onPerFrameSlider(CCObject* sender);
    void onSizeSlider(CCObject* sender);
    void onFramesSlider(CCObject* sender);
    void onGray(CCObject*);

    void stepMethod(int dir);
    void refreshLabels();
    void loadGifBytes(std::vector<uint8_t> const& bytes);
    void scheduleRecompute();
    void recompute(float = 0.f);
    void tick(float);
    void applyPreview(std::shared_ptr<PreviewJob> job);
    void finishBuild(std::shared_ptr<BuildJob> job);
    void buildPreview();
    void setStatus(std::string const& a, std::string const& b = "", std::string const& note = "");

    // Indice del metodo 18 (capas) en la lista de la ventana: va despues de los 17 metodos de imagen.
    static constexpr int kLayeredIndex = int(gdx::Method::Ascii);
    bool layered() const { return m_method == kLayeredIndex; }
    gdx::Method method() const { return layered() ? gdx::Method::BSP : gdx::Method(m_method); }
    gdx::LayeredOptions makeLayeredOptions() const;
    std::string layeredKey() const;
    gdx::Options makeOptions(int perFrame) const;
    gdx::GdExportOptions makeExportOptions() const;
    gdx::GifPlan makePlan() const;
    int effectivePerFrame(int frames) const;
    int maxTotal() const;
    CCPoint placementOrigin() const;
    int maxUsedGroup() const;

    EditorUI* m_editor = nullptr;
    std::shared_ptr<const gdx::GifData> m_gif;
    std::shared_ptr<PreviewJob> m_job;
    std::shared_ptr<BuildJob> m_build;
    int m_jobCounter = 0;
    std::shared_ptr<gdx::LayeredAnim> m_layered;   // metodo 18: capas calculadas por la vista previa (se reutilizan al colocar)
    gdx::GifPlan m_layeredPlan;
    std::string m_layeredKey;
    gdx::Result m_result;                 // frame 0 convertido (vista previa)
    bool m_hasResult = false;

    int m_method = 2;                     // 0..17 (17 = capas; BSP por defecto)
    int m_perFrame = 300;                 // objetos por frame
    int m_sizeU = 600;                    // lado largo, unidades GD
    int m_maxFrames = 40;                 // frames maximos a usar
    bool m_gray = false;

    CCLabelBMFont* m_methodLabel = nullptr;
    CCLabelBMFont* m_descLabel = nullptr;
    CCLabelBMFont* m_perFrameValue = nullptr;
    CCLabelBMFont* m_sizeValue = nullptr;
    CCLabelBMFont* m_framesValue = nullptr;
    Slider* m_perFrameSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    Slider* m_framesSlider = nullptr;
    CCMenuItemToggler* m_chkGray = nullptr;
    CCLabelBMFont* m_statA = nullptr;
    CCLabelBMFont* m_statB = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
};
