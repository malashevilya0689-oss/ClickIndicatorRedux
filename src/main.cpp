#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>

using namespace geode::prelude;

struct ClickWindow {
    float idealX;
    float minX;
    float maxX;
    float targetY;
    int frameCount;
    bool isOrb;
    bool isRecorded;
};

struct CachedHazard {
    float x;
    float y;
    float width;
    float height;
    bool isOrb;
};

class IndicatorEngine {
public:
    std::vector<CachedHazard> levelObjects;
    std::vector<ClickWindow> activeWindows;
    std::vector<ClickWindow> recordedClicks;

    float currentFPS = 240.0f;
    float playerSpeedUPS = 311.58f;
    float lastOffsetMs = 0.0f;
    int lastOffsetFrames = 0;
    std::string lastJudgeText = "READY";
    ccColor3B lastJudgeColor = {26, 255, 102};
    bool usedAssistThisAttempt = false;
    bool objectsCached = false;

    static IndicatorEngine& get() {
        static IndicatorEngine instance;
        return instance;
    }

    void clearLevelCache() {
        levelObjects.clear();
        activeWindows.clear();
        objectsCached = false;
        lastJudgeText = "READY";
        lastOffsetMs = 0.0f;
        lastOffsetFrames = 0;
    }

    float computePlayerSpeedUPS(PlayerObject* player) {
        if (!player) return 311.58f;
        float mult = player->m_playerSpeed / 0.9f;
        return 311.58f * mult;
    }

    void cacheObjectsFromPlayLayer(PlayLayer* pl) {
        if (objectsCached || !pl || !pl->m_objects) return;
        objectsCached = true;
        levelObjects.clear();

        unsigned int count = pl->m_objects->count();
        for (unsigned int i = 0; i < count; ++i) {
            auto go = typeinfo_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
            if (!go) continue;

 auto t = go->m_objectType;
            bool isOrb = go->m_isRing;
            bool isHazard = (t == GameObjectType::Hazard || t == GameObjectType::AnimatedHazard || go->m_hasNoGlow);
            if (isOrb || isHazard) {  
                float w = go->getContentSize().width * std::abs(go->getScaleX());
                float h = go->getContentSize().height * std::abs(go->getScaleY());
                if (w <= 1.0f) w = 30.0f;
                if (h <= 1.0f) h = 30.0f;

                levelObjects.push_back({
                    go->getPositionX(),
                    go->getPositionY(),
                    w,
                    h,
                    isOrb
                });
            }
        }

        std::sort(levelObjects.begin(), levelObjects.end(), [](const CachedHazard& a, const CachedHazard& b) {
            return a.x < b.x;
        });
    }

    void solveUpcomingWindows(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;

        PlayerObject* p1 = pl->m_player1;
        float px = p1->getPositionX();
        float py = p1->getPositionY();
        float lookahead = static_cast<float>(Mod::get()->getSettingValue<double>("lookahead-distance"));
        std::string mode = Mod::get()->getSettingValue<std::string>("solver-mode");

        playerSpeedUPS = computePlayerSpeedUPS(p1);
        float unitsPerFrame = playerSpeedUPS / std::max(30.0f, currentFPS);

        activeWindows.clear();

        if (mode != "Real-Time Auto-Solver Only") {
            for (auto& rec : recordedClicks) {
                if (rec.maxX >= px - 60.0f && rec.minX <= px + lookahead) {
                    rec.frameCount = std::max(1, static_cast<int>(std::round((rec.maxX - rec.minX) / unitsPerFrame)));
                    activeWindows.push_back(rec);
                }
            }
        }

        if (mode != "Recorded Clicks Only") {
            auto it = std::lower_bound(
                levelObjects.begin(), levelObjects.end(), px - 40.0f,
                [](const CachedHazard& a, float val) { return a.x < val; }
            );

            float lastAddedX = -99999.0f;

            for (; it != levelObjects.end() && it->x <= px + lookahead; ++it) {
                if (std::abs(it->x - lastAddedX) < 42.0f) continue;
                if (std::abs(it->y - py) > 135.0f) continue;

                ClickWindow win{};
                win.isOrb = it->isOrb;
                win.isRecorded = false;

                if (it->isOrb) {
                    float halfHitbox = it->width * 0.55f;
                    win.minX = it->x - halfHitbox;
                    win.maxX = it->x + halfHitbox * 0.65f;
                    win.idealX = it->x - halfHitbox * 0.25f;
                    win.targetY = it->y;
                } else {
                    float speedRatio = p1->m_playerSpeed / 0.9f;
                    float windowSpan = std::max(unitsPerFrame * 3.0f, 18.0f * speedRatio);

                    win.maxX = (it->x - it->width * 0.45f) - 4.0f;
                    win.minX = win.maxX - windowSpan;
                    win.idealX = (win.minX + win.maxX) * 0.5f;
                    win.targetY = py;
                }

                bool duplicate = false;
                for (const auto& existing : activeWindows) {
                    if (std::abs(existing.idealX - win.idealX) < 28.0f) {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate) {
                    win.frameCount = std::max(1, static_cast<int>(std::round((win.maxX - win.minX) / unitsPerFrame)));
                    activeWindows.push_back(win);
                    lastAddedX = it->x;
                }
            }
        }

        std::sort(activeWindows.begin(), activeWindows.end(), [](const ClickWindow& a, const ClickWindow& b) {
            return a.idealX < b.idealX;
        });
    }

    void evaluateClick(float clickX, float clickY) {
        float unitsPerFrame = playerSpeedUPS / std::max(30.0f, currentFPS);

        ClickWindow* bestWin = nullptr;
        float bestDist = 999999.0f;

        for (auto& win : activeWindows) {
            float d = std::abs(clickX - win.idealX);
            if (d < bestDist && d < 95.0f) {
                bestDist = d;
                bestWin = &win;
            }
        }

        if (bestWin) {
            float deltaUnits = clickX - bestWin->idealX;
            lastOffsetMs = (deltaUnits / playerSpeedUPS) * 1000.0f;
            lastOffsetFrames = static_cast<int>(std::round(deltaUnits / unitsPerFrame));

            if (std::abs(lastOffsetMs) <= (1000.0f / currentFPS) * 0.65f) {
                lastJudgeText = (bestWin->frameCount <= 1) ? "FRAME PERFECT" : "PERFECT";
                lastJudgeColor = {26, 255, 102};
            } else if (clickX >= bestWin->minX && clickX <= bestWin->maxX) {
                lastJudgeText = "GOOD";
                lastJudgeColor = {140, 255, 90};
            } else if (deltaUnits < 0) {
                lastJudgeText = "EARLY";
                lastJudgeColor = {255, 210, 50};
            } else {
                lastJudgeText = "LATE";
                lastJudgeColor = {255, 75, 75};
            }
        } else {
            lastOffsetMs = 0.0f;
            lastOffsetFrames = 0;
            lastJudgeText = "PERFECT";
            lastJudgeColor = {26, 255, 102};
        }

        std::string mode = Mod::get()->getSettingValue<std::string>("solver-mode");
        if (mode != "Real-Time Auto-Solver Only") {
            for (auto& rec : recordedClicks) {
                if (std::abs(rec.idealX - clickX) < 25.0f) {
                    rec.idealX = (rec.idealX + clickX) * 0.5f;
                    rec.targetY = clickY;
                    return;
                }
            }
            float span = std::max(unitsPerFrame * 2.5f, 12.0f);
            recordedClicks.push_back({
                clickX,
                clickX - span,
                clickX + span,
                clickY,
                3,
                false,
                true
            });
        }
    }
};

class ClickIndicatorDashboard : public FLAlertLayer {
protected:
    void onToggleMod(CCObject*) {
        bool val = !Mod::get()->getSettingValue<bool>("enabled");
        Mod::get()->setSettingValue("enabled", val);
        Notification::create(val ? "Click Indicator: ON" : "Click Indicator: OFF", NotificationIcon::Info)->show();
    }

    void onToggleSafeMode(CCObject*) {
        bool val = !Mod::get()->getSettingValue<bool>("safe-mode");
        Mod::get()->setSettingValue("safe-mode", val);
        Notification::create(val ? "Safe Mode: ENABLED" : "Safe Mode: DISABLED", NotificationIcon::Success)->show();
    }

    void onToggleHighway(CCObject*) {
        bool val = !Mod::get()->getSettingValue<bool>("show-highway");
        Mod::get()->setSettingValue("show-highway", val);
        Notification::create(val ? "3D Highway HUD: ON" : "3D Highway HUD: OFF", NotificationIcon::Info)->show();
    }

    void onClearRecorded(CCObject*) {
        IndicatorEngine::get().recordedClicks.clear();
        Notification::create("Cleared recorded clicks!", NotificationIcon::Warning)->show();
    }

    void onOpenFullGeodeSettings(CCObject*) {
        geode::openSettingsPopup(Mod::get());
    }

    void onClosePopup(CCObject*) {
        this->keyBackClicked();
    }

public:
    static ClickIndicatorDashboard* create() {
        auto ret = new ClickIndicatorDashboard();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    bool init() {
        if (!FLAlertLayer::init(165)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto bg = cocos2d::extension::CCScale9Sprite::create("GJ_square02.png");
        bg->setContentSize({350.0f, 240.0f});
        bg->setPosition(winSize / 2);
        m_mainLayer->addChild(bg);

        auto title = CCLabelBMFont::create("Click Indicator Redux", "goldFont.fnt");
        title->setScale(0.75f);
        title->setPosition({winSize.width / 2, winSize.height / 2 + 95.0f});
        m_mainLayer->addChild(title);

        float fps = IndicatorEngine::get().currentFPS;
        char fpsBuf[128];
        std::snprintf(fpsBuf, sizeof(fpsBuf), "Real-Time Solver | %.0f FPS (%.2f ms/frame)", fps, 1000.0f / std::max(1.0f, fps));
        auto fpsInfo = CCLabelBMFont::create(fpsBuf, "chatFont.fnt");
        fpsInfo->setScale(0.6f);
        fpsInfo->setColor({26, 255, 102});
        fpsInfo->setPosition({winSize.width / 2, winSize.height / 2 + 72.0f});
        m_mainLayer->addChild(fpsInfo);

        auto menu = CCMenu::create();
        menu->setPosition(winSize / 2);
        m_mainLayer->addChild(menu);

        auto closeSpr = CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png");
        closeSpr->setScale(0.85f);
        auto closeBtn = CCMenuItemSpriteExtra::create(closeSpr, this, menu_selector(ClickIndicatorDashboard::onClosePopup));
        closeBtn->setPosition({-160.0f, 105.0f});
        menu->addChild(closeBtn);

        auto btnModSpr = ButtonSprite::create("Toggle Indicator", 135, true, "bigFont.fnt", "GJ_button_01.png", 30.0f, 0.5f);
        auto btnMod = CCMenuItemSpriteExtra::create(btnModSpr, this, menu_selector(ClickIndicatorDashboard::onToggleMod));
        btnMod->setPosition({-78.0f, 30.0f});
        menu->addChild(btnMod);

        auto btnSafeSpr = ButtonSprite::create("Toggle Safe Mode", 135, true, "bigFont.fnt", "GJ_button_02.png", 30.0f, 0.5f);
        auto btnSafe = CCMenuItemSpriteExtra::create(btnSafeSpr, this, menu_selector(ClickIndicatorDashboard::onToggleSafeMode));
        btnSafe->setPosition({78.0f, 30.0f});
        menu->addChild(btnSafe);

        auto btnHwySpr = ButtonSprite::create("Toggle 3D Highway", 135, true, "bigFont.fnt", "GJ_button_04.png", 30.0f, 0.5f);
        auto btnHwy = CCMenuItemSpriteExtra::create(btnHwySpr, this, menu_selector(ClickIndicatorDashboard::onToggleHighway));
        btnHwy->setPosition({-78.0f, -15.0f});
        menu->addChild(btnHwy);

        auto btnClrSpr = ButtonSprite::create("Reset Clicks", 135, true, "bigFont.fnt", "GJ_button_06.png", 30.0f, 0.5f);
        auto btnClr = CCMenuItemSpriteExtra::create(btnClrSpr, this, menu_selector(ClickIndicatorDashboard::onClearRecorded));
        btnClr->setPosition({78.0f, -15.0f});
        menu->addChild(btnClr);

        auto fullSpr = ButtonSprite::create("Colors, Opacity & All Settings...", 270, true, "goldFont.fnt", "GJ_button_01.png", 34.0f, 0.65f);
        auto fullBtn = CCMenuItemSpriteExtra::create(fullSpr, this, menu_selector(ClickIndicatorDashboard::onOpenFullGeodeSettings));
        fullBtn->setPosition({0.0f, -72.0f});
        menu->addChild(fullBtn);

        this->setKeypadEnabled(true);
        this->setTouchEnabled(true);
        return true;
    }
};

class $modify(IndicatorPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        IndicatorEngine::get().clearLevelCache();
        IndicatorEngine::get().recordedClicks.clear();
        IndicatorEngine::get().usedAssistThisAttempt = false;

        auto worldDraw = CCDrawNode::create();
        worldDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        worldDraw->setZOrder(9999);
        worldDraw->setTag(778801);
        if (m_objectLayer) {
            m_objectLayer->addChild(worldDraw);
        }

        auto hudDraw = CCDrawNode::create();
        hudDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        hudDraw->setZOrder(1000);
        hudDraw->setTag(778802);
        this->addChild(hudDraw);

        auto judgeLabel = CCLabelBMFont::create("PERFECT", "bigFont.fnt");
        judgeLabel->setScale(0.28f);
        judgeLabel->setZOrder(1001);
        judgeLabel->setTag(778803);
        this->addChild(judgeLabel);

        auto msLabel = CCLabelBMFont::create("0.00 ms", "chatFont.fnt");
        msLabel->setScale(0.48f);
        msLabel->setZOrder(1001);
        msLabel->setTag(778804);
        this->addChild(msLabel);

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        IndicatorEngine::get().usedAssistThisAttempt = Mod::get()->getSettingValue<bool>("enabled");
    }

    void levelComplete() {
        bool safeMode = Mod::get()->getSettingValue<bool>("safe-mode");
        bool autoSafe = Mod::get()->getSettingValue<bool>("auto-safe-mode") && IndicatorEngine::get().usedAssistThisAttempt;

        if (safeMode || autoSafe) {
            bool origTestMode = m_isTestMode;
            m_isTestMode = true;
            PlayLayer::levelComplete();
            m_isTestMode = origTestMode;
            Notification::create("Safe Mode Active: Progress not saved", NotificationIcon::Info)->show();
            return;
        }
        PlayLayer::levelComplete();
    }

    void renderIndicators() {
        if (!m_objectLayer || !m_player1) return;

        auto worldDraw = typeinfo_cast<CCDrawNode*>(m_objectLayer->getChildByTag(778801));
        auto hudDraw = typeinfo_cast<CCDrawNode*>(this->getChildByTag(778802));
        auto judgeLabel = typeinfo_cast<CCLabelBMFont*>(this->getChildByTag(778803));
        auto msLabel = typeinfo_cast<CCLabelBMFont*>(this->getChildByTag(778804));

        if (!worldDraw || !hudDraw || !judgeLabel || !msLabel) return;

        worldDraw->clear();
        hudDraw->clear();

        bool enabled = Mod::get()->getSettingValue<bool>("enabled");
        if (!enabled) {
            judgeLabel->setVisible(false);
            msLabel->setVisible(false);
            return;
        }

        auto& eng = IndicatorEngine::get();
        eng.usedAssistThisAttempt = true;
        eng.cacheObjectsFromPlayLayer(this);
        eng.solveUpcomingWindows(this);

        ccColor3B baseCol = Mod::get()->getSettingValue<ccColor3B>("primary-color");
        float winAlpha = static_cast<float>(Mod::get()->getSettingValue<int64_t>("window-opacity")) / 255.0f;
        float laserAlpha = static_cast<float>(Mod::get()->getSettingValue<int64_t>("laser-opacity")) / 255.0f;
        float glowWidth = static_cast<float>(Mod::get()->getSettingValue<double>("glow-intensity"));

        ccColor4F colFill = {baseCol.r / 255.f, baseCol.g / 255.f, baseCol.b / 255.f, winAlpha};
        ccColor4F colGlow = {baseCol.r / 255.f, baseCol.g / 255.f, baseCol.b / 255.f, laserAlpha * 0.35f};
        ccColor4F colCore = {baseCol.r / 255.f, baseCol.g / 255.f, baseCol.b / 255.f, laserAlpha};
        ccColor4F colWhite = {0.9f, 1.0f, 0.95f, laserAlpha};

        float px = m_player1->getPositionX();
        float py = m_player1->getPositionY();

        if (Mod::get()->getSettingValue<bool>("show-world-bars")) {
            float bottomY = py - 600.0f;
            float topY = py + 600.0f;

            worldDraw->drawSegment({px, bottomY}, {px, topY}, glowWidth * 0.9f, colGlow);
            worldDraw->drawSegment({px, bottomY}, {px, topY}, 1.3f, {1.0f, 1.0f, 1.0f, 0.85f});

            for (const auto& win : eng.activeWindows) {
                CCPoint rectVerts[4] = {
                    {win.minX, bottomY},
                    {win.maxX, bottomY},
                    {win.maxX, topY},
                    {win.minX, topY}
                };
                worldDraw->drawPolygon(rectVerts, 4, colFill, 0.0f, {0, 0, 0, 0});

                worldDraw->drawSegment({win.idealX, bottomY}, {win.idealX, topY}, glowWidth, colGlow);
                worldDraw->drawSegment({win.idealX, bottomY}, {win.idealX, topY}, 1.1f, colCore);

                float ny = win.targetY;
                worldDraw->drawSegment({win.minX, ny}, {win.maxX, ny}, 1.0f, colCore);
                worldDraw->drawSegment({win.minX, ny - 3.5f}, {win.minX, ny + 3.5f}, 0.9f, colCore);
                worldDraw->drawSegment({win.maxX, ny - 3.5f}, {win.maxX, ny + 3.5f}, 0.9f, colCore);

                CCPoint nodeSq[4] = {
                    {win.idealX - 3.2f, ny - 3.2f},
                    {win.idealX + 3.2f, ny - 3.2f},
                    {win.idealX + 3.2f, ny + 3.2f},
                    {win.idealX - 3.2f, ny + 3.2f}
                };
                worldDraw->drawPolygon(nodeSq, 4, colCore, 0.7f, colWhite);
            }
        }

        bool showHwy = Mod::get()->getSettingValue<bool>("show-highway");
        judgeLabel->setVisible(showHwy);
        msLabel->setVisible(showHwy);

        if (showHwy) {
            float scale = static_cast<float>(Mod::get()->getSettingValue<double>("highway-scale"));
            float cx = static_cast<float>(Mod::get()->getSettingValue<double>("highway-x"));
            float botY = 92.0f;
            float topY = botY + 108.0f * scale;
            float botHalfW = 22.0f * scale;
            float topHalfW = 9.0f * scale;

            CCPoint bgQuad[4] = {
                {cx - botHalfW, botY},
                {cx + botHalfW, botY},
                {cx + topHalfW, topY},
                {cx - topHalfW, topY}
            };
            hudDraw->drawPolygon(bgQuad, 4, {0.02f, 0.12f, 0.05f, 0.72f}, 1.2f * scale, colCore);

            hudDraw->drawSegment({cx - botHalfW, botY}, {cx - topHalfW, topY}, 1.8f * scale, colCore);
            hudDraw->drawSegment({cx + botHalfW, botY}, {cx + topHalfW, topY}, 1.8f * scale, colCore);

            CCPoint judgeBar[4] = {
                {cx - botHalfW - 3.0f * scale, botY - 3.5f * scale},
                {cx + botHalfW + 3.0f * scale, botY - 3.5f * scale},
                {cx + botHalfW + 3.0f * scale, botY + 3.5f * scale},
                {cx - botHalfW - 3.0f * scale, botY + 3.5f * scale}
            };
            hudDraw->drawPolygon(judgeBar, 4, colCore, 0.8f, colWhite);

            float aw = 6.0f * scale;
            hudDraw->drawSegment({cx - botHalfW - 9.0f * scale, botY + aw}, {cx - botHalfW - 4.0f * scale, botY}, 1.2f * scale, colWhite);
            hudDraw->drawSegment({cx - botHalfW - 9.0f * scale, botY - aw}, {cx - botHalfW - 4.0f * scale, botY}, 1.2f * scale, colWhite);

            hudDraw->drawSegment({cx + botHalfW + 9.0f * scale, botY + aw}, {cx + botHalfW + 4.0f * scale, botY}, 1.2f * scale, colWhite);
            hudDraw->drawSegment({cx + botHalfW + 9.0f * scale, botY - aw}, {cx + botHalfW + 4.0f * scale, botY}, 1.2f * scale, colWhite);

            float maxPreviewDist = 360.0f;
            for (const auto& win : eng.activeWindows) {
                float dist = win.idealX - px;
                if (dist < -15.0f || dist > maxPreviewDist) continue;

                float t = std::clamp(dist / maxPreviewDist, 0.0f, 1.0f);
                float perspT = std::pow(t, 0.82f);
                float noteY = botY + perspT * (topY - botY);
                float halfW = botHalfW + perspT * (topHalfW - botHalfW);
                float noteH = std::max(2.2f, (1.0f - perspT * 0.55f) * 5.0f * scale);

                CCPoint noteQuad[4] = {
                    {cx - halfW + 1.5f, noteY - noteH},
                    {cx + halfW - 1.5f, noteY - noteH},
                    {cx + halfW - 1.5f, noteY + noteH},
                    {cx - halfW + 1.5f, noteY + noteH}
                };
                hudDraw->drawPolygon(noteQuad, 4, colCore, 0.6f, colWhite);
            }

            judgeLabel->setString(eng.lastJudgeText.c_str());
            judgeLabel->setColor(eng.lastJudgeColor);
            judgeLabel->setPosition({cx, botY - 14.0f * scale});

            char msBuf[64];
            std::snprintf(msBuf, sizeof(msBuf), "%+.2f ms (%+df)", eng.lastOffsetMs, eng.lastOffsetFrames);
            msLabel->setString(msBuf);
            msLabel->setColor({190, 220, 200});
            msLabel->setPosition({cx, botY - 24.0f * scale});
        }
    }
};

class $modify(IndicatorBaseGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);

        if (auto pl = typeinfo_cast<PlayLayer*>(this)) {
            float realDt = CCDirector::sharedDirector()->getDeltaTime();
            if (realDt > 0.0001f) {
                float instantFPS = 1.0f / realDt;
                IndicatorEngine::get().currentFPS = IndicatorEngine::get().currentFPS * 0.9f + instantFPS * 0.1f;
            }
            static_cast<IndicatorPlayLayer*>(pl)->renderIndicators();
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (down && isPlayer1 && button == 1) {
            if (auto pl = typeinfo_cast<PlayLayer*>(this)) {
                if (pl->m_player1 && Mod::get()->getSettingValue<bool>("enabled")) {
                    IndicatorEngine::get().evaluateClick(
                        pl->m_player1->getPositionX(),
                        pl->m_player1->getPositionY()
                    );
                }
            }
        }
    }
};

class $modify(IndicatorSafeLevel, GJGameLevel) {
    void savePercentage(int percent, bool isPractice, int clicks, int attempts, bool isValid) {
        bool safeMode = Mod::get()->getSettingValue<bool>("safe-mode");
        bool autoSafe = Mod::get()->getSettingValue<bool>("auto-safe-mode") && IndicatorEngine::get().usedAssistThisAttempt;

        if (safeMode || autoSafe) {
            return;
        }
        GJGameLevel::savePercentage(percent, isPractice, clicks, attempts, isValid);
    }
};

class $modify(IndicatorPauseLayer, PauseLayer) {
    void onOpenIndicatorMenu(CCObject*) {
        ClickIndicatorDashboard::create()->show();
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        auto circleSpr = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_optionsBtn02_001.png",
            0.95f,
            CircleBaseColor::Green,
            CircleBaseSize::Small
        );

        auto btn = CCMenuItemSpriteExtra::create(
            circleSpr,
            this,
            menu_selector(IndicatorPauseLayer::onOpenIndicatorMenu)
        );

        auto pauseMenu = CCMenu::create();
        pauseMenu->setPosition({winSize.width - 42.0f, winSize.height - 42.0f});
        pauseMenu->addChild(btn);
        pauseMenu->setZOrder(100);
        this->addChild(pauseMenu);
    }
};
