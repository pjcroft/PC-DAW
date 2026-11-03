#include <JuceHeader.h>
#include "MainComponent.h"
#include "PluginWindow.h"
#include "DemoContent.h"

//==============================================================================
class TapedeckApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Tapedeck"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        engine = std::make_unique<te::Engine> ("Tapedeck", std::make_unique<TapedeckUIBehaviour>(), nullptr);

        // Developer options:
        //   --demo                fill a scratch session with generated guitar/drum clips
        //   --screenshot <file>   save a PNG of the window after start-up, then quit
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.trim();

        juce::File initialSession;

        if (args.contains ("--demo"))
            initialSession = DemoContent::prepareDemoSession (*engine);

        mainWindow = std::make_unique<MainWindow> (*engine, initialSession);

        auto shotIndex = args.indexOf ("--screenshot");

        if (shotIndex >= 0 && shotIndex + 1 < args.size())
        {
            auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args[shotIndex + 1].unquoted());

            juce::Timer::callAfterDelay (2500, [this, file]
            {
                if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    mc->saveScreenshot (file);

                systemRequestedQuit();
            });
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        engine = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mc->saveBeforeQuit();

        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (te::Engine& engine, const juce::File& initialSession)
            : DocumentWindow ("Tapedeck", Theme::bgDeep, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (engine, initialSession);
            setContentOwned (content, true);

           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (content);
           #else
            setMenuBar (content, 24);
           #endif

            setResizable (true, true);
            setResizeLimits (900, 500, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->grabKeyboardFocus();
        }

        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #else
            setMenuBar (nullptr);
           #endif
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (TapedeckApplication)
