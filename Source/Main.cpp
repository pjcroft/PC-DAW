#include <JuceHeader.h>
#include "MainComponent.h"
#include "PluginWindow.h"
#include "DemoContent.h"
#include "Logo.h"

//==============================================================================
class CroftApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Croft DAW"; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String& commandLine) override
    {
        engine = std::make_unique<te::Engine> ("Croft DAW", std::make_unique<CroftUIBehaviour>(), nullptr);

        // Developer options:
        //   --demo                fill a scratch session with generated guitar/drum clips
        //   --screenshot <file>   save a PNG of the window after start-up, then quit
        //   --export-stems <dir>  render stems of the opened session, then quit
        auto args = juce::StringArray::fromTokens (commandLine, true);
        args.trim();

        //   --render-icon <file.png>   writes the launcher icon (1024 px) and quits
        if (auto i = args.indexOf ("--render-icon"); i >= 0 && i + 1 < args.size())
        {
            auto f = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
            f.deleteFile();
            juce::FileOutputStream out (f);
            juce::PNGImageFormat().writeImageToStream (Logo::createIconImage (1024), out);
            out.flush();
            quit();
            return;
        }

        juce::File initialSession;

        if (args.contains ("--demo"))
            initialSession = DemoContent::prepareDemoSession (*engine);

        mainWindow = std::make_unique<MainWindow> (*engine, initialSession);

        if (args.contains ("--test-record"))
        {
            juce::MessageManager::callAsync ([this]
            {
                if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    mc->runRecordingSelfTest ([this] (bool ok)
                    {
                        testFailed = ! ok;
                        systemRequestedQuit();
                    });
            });
        }

        auto stemsIndex = args.indexOf ("--export-stems");

        if (stemsIndex >= 0 && stemsIndex + 1 < args.size())
        {
            auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[stemsIndex + 1].unquoted());

            juce::MessageManager::callAsync ([this, dir]
            {
                if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    for (auto& f : mc->exportStemsTo (dir))
                        std::cout << "Wrote " << f.getFullPathName() << std::endl;

                systemRequestedQuit();
            });
        }

        //   --mixer hidden|docked|window
        if (auto mi = args.indexOf ("--mixer"); mi >= 0 && mi + 1 < args.size())
            if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mc->setMixerModeByName (args[mi + 1]);

        auto shotIndex = args.indexOf ("--screenshot");

        if (shotIndex >= 0 && shotIndex + 1 < args.size())
        {
            auto file = juce::File::getCurrentWorkingDirectory().getChildFile (args[shotIndex + 1].unquoted());

            juce::Timer::callAfterDelay (6000, [this, file]
            {
                if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    mc->saveScreenshot (file);

                systemRequestedQuit();
            });
        }
    }

    void shutdown() override
    {
        if (testFailed)
            setApplicationReturnValue (1);

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
            : DocumentWindow ("Croft DAW", Theme::bgDeep, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent (engine, initialSession);
            setContentOwned (content, true);

           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (content);
           #else
            setMenuBar (content, 24);
           #endif

            setIcon (Logo::createIconImage (256));
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
    bool testFailed = false;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (CroftApplication)
