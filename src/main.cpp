m_fields->hudDraw->drawSegment({cx + botHalfW + 9.0f * scale, botY + aw}, {cx + botHalfW + 4.0f * scale, botY}, 1.2f * scale, colWhite);
            m_fields->hudDraw->drawSegment({cx + botHalfW + 9.0f * scale, botY - aw}, {cx + botHalfW + 4.0f * scale, botY}, 1.2f * scale, colWhite);

            // Спускающиеся ноты/полоски предстоящих кликов с 3D-перспективой
            float maxPreviewDist = 360.0f;
            for (const auto& win : eng.activeWindows) {
                float dist = win.idealX - px;
                if (dist < -15.0f || dist > maxPreviewDist) continue;

                float t = std::clamp(dist / maxPreviewDist, 0.0f, 1.0f);
                // Перспективное сжатие к верхушке дорожки
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
                m_fields->hudDraw->drawPolygon(noteQuad, 4, colCore, 0.6f, colWhite);
            }

            // Обновление текста PERFECT и миллисекунд под 3D-дорожкой
            m_fields->judgeLabel->setString(eng.lastJudgeText.c_str());
            m_fields->judgeLabel->setColor(eng.lastJudgeColor);
            m_fields->judgeLabel->setPosition({cx, botY - 14.0f * scale});

            m_fields->msLabel->setString(
                fmt::format("{:+.2f} ms ({:+d}f)", eng.lastOffsetMs, eng.lastOffsetFrames).c_str()
            );
            m_fields->msLabel->setColor({190, 220, 200});
            m_fields->msLabel->setPosition({cx, botY - 24.0f * scale});
        }
    }
};

// Хук GJBaseGameLayer для перехвата каждого физического шага и кликов игрока
class $modify( IndicatorBaseGameLayer, GJBaseGameLayer ) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);

        if (auto pl = typeinfo_cast<PlayLayer*>(this)) {
            float realDt = CCDirector::sharedDirector()->getDeltaTime();
            if (realDt > 0.0001f) {
                float instantFPS = 1.0f / realDt;
                // Плавное сглаживание показателя FPS
                IndicatorEngine::get().currentFPS = IndicatorEngine::get().currentFPS * 0.9f + instantFPS * 0.1f;
            }
            static_cast<IndicatorPlayLayer*>(pl)->renderIndicators();
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        // Фиксируем момент нажатия прыжка (button == 1) первого игрока
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

// Дополнительная защита Safe Mode от сохранения процентов рекорда
class $modify( IndicatorSafeLevel, GJGameLevel ) {
    void savePercentage(int percent, bool isPractice, int clicks, int attempts, bool isValid) {
        bool safeMode = Mod::get()->getSettingValue<bool>("safe-mode");
        bool autoSafe = Mod::get()->getSettingValue<bool>("auto-safe-mode") && IndicatorEngine::get().usedAssistThisAttempt;

        if (safeMode || autoSafe) {
            return;
        }
        GJGameLevel::savePercentage(percent, isPractice, clicks, attempts, isValid);
    }
};

// Добавление круглой кнопки настроек в меню паузы (PauseLayer)
class $modify( IndicatorPauseLayer, PauseLayer ) {void onOpenIndicatorMenu(CCObject*) {
        ClickIndicatorDashboard::create()->show();
    }

    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        // Создаем круглую зеленую кнопку со значком шестеренки/индикатора
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
