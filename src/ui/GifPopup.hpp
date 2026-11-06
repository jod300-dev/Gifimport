#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "gifpatch.hpp"

using namespace geode::prelude;

// Ventana del importador de GIF (metodo de parches):
//   izquierda: vista previa del frame 0 + estado
//   derecha: objetos por frame, lado largo, escala de grises, y botones GIF / Colocar / Ajustes
class GifPopup : public geode::Popup {
public:
    static GifPopup* create(EditorUI* editor);
    ~GifPopup();

protected:
    // Fondo + parche 0 (vista previa) en un hilo aparte
    struct PreviewJob {
        gdx::Progress prog;
        std::shared_ptr<const gdx::GifData> gif;
        gdx::GifPlan plan;
        gdx::Options opt;
        gdx::PatchParams pp;
        int id = 0;
        gdx::PatchSet patchSet;
        gdx::Result result;               // fondo + parche 0 combinados
        std::atomic<bool> done{false};
    };
    // Conversion de todos los frames (al pulsar Colocar)
    struct BuildJob {
        gdx::Progress prog;
        std::shared_ptr<const gdx::GifData> gif;
        gdx::GifPlan plan;
        gdx::Options opt;
        gdx::PatchParams pp;
        gdx::PatchSet patchSet;
        std::atomic<bool> done{false};
    };

    bool init(EditorUI* editor);

    void onPick(CCObject*);
    void onPlace(CCObject*);
    void onInfo(CCObject*);
    void onSettings(CCObject*);
    void onPerFrameSlider(CCObject* sender);
    void onSizeSlider(CCObject* sender);
    void onGray(CCObject*);

    void refreshLabels();
    void loadGifBytes(std::vector<uint8_t> const& bytes);
    void scheduleRecompute();
    void recompute(float = 0.f);
    void tick(float);
    void applyPreview(std::shared_ptr<PreviewJob> job);
    void finishBuild(std::shared_ptr<BuildJob> job);
    void buildPreview();
    void setStatus(std::string const& a, std::string const& b = "", std::string const& note = "");

    gdx::Options makeOptions(int perFrame) const;
    gdx::PatchParams makePatchParams(int baseBudget) const;
    gdx::GdExportOptions makeExportOptions() const;
    gdx::GifPlan makePlan() const;
    int maxTotal() const;
    int baseBudget() const;
    int patchBudget(int frames) const;
    CCPoint placementOrigin() const;
    int maxUsedGroup() const;

    EditorUI* m_editor = nullptr;
    std::shared_ptr<const gdx::GifData> m_gif;
    std::shared_ptr<PreviewJob> m_job;
    std::shared_ptr<BuildJob> m_build;
    int m_jobCounter = 0;
    gdx::PatchSet m_patchSet;             // fondo + parche 0 (vista previa)
    gdx::Result m_result;                 // fondo + parche 0 combinados
    bool m_hasResult = false;

    int m_perFrame = 300;                 // objetos por frame (limite de cada parche)
    int m_sizeU = 600;                    // lado largo, unidades GD
    bool m_gray = false;

    CCLabelBMFont* m_perFrameValue = nullptr;
    CCLabelBMFont* m_sizeValue = nullptr;
    Slider* m_perFrameSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    CCMenuItemToggler* m_chkGray = nullptr;
    CCLabelBMFont* m_statA = nullptr;
    CCLabelBMFont* m_statB = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
};
