#pragma once

// Plugin editor windows. Adapted from the Tracktion Engine examples (GPLv3).

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
   #if JUCE_LINUX
    static constexpr bool addToDesktopImmediately = false;
   #else
    static constexpr bool addToDesktopImmediately = true;
   #endif

    explicit PluginWindow (te::Plugin& p)
        : DocumentWindow (p.getName(), juce::Colours::black, DocumentWindow::closeButton, addToDesktopImmediately),
          plugin (p)
    {
        getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
        setResizeLimits (100, 50, 4000, 4000);
        setUsingNativeTitleBar (true);
        recreateEditor();
        setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());

       #if JUCE_LINUX
        setAlwaysOnTop (true);
        addToDesktop();
       #endif

        updateStoredBounds = true;
    }

    ~PluginWindow() override
    {
        updateStoredBounds = false;
        plugin.edit.flushPluginStateIfNeeded (plugin);
        setEditor (nullptr);
    }

    static std::unique_ptr<juce::Component> create (te::Plugin& plugin)
    {
        if (auto ext = dynamic_cast<te::ExternalPlugin*> (&plugin))
            if (ext->getAudioPluginInstance() == nullptr)
                return {};

        auto w = std::make_unique<PluginWindow> (plugin);

        if (w->editor == nullptr)
            return {};

        w->setVisible (true);
        w->toFront (false);
        return w;
    }

    void recreateEditor()
    {
        setEditor (nullptr);
        setEditor (plugin.createEditor());
    }

    void recreateEditorAsync()
    {
        setEditor (nullptr);
        juce::Timer::callAfterDelay (50, [sp = SafePointer<PluginWindow> (this)]
        {
            if (sp != nullptr)
                sp->recreateEditor();
        });
    }

private:
    void setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEditor)
    {
        JUCE_AUTORELEASEPOOL
        {
            setConstrainer (nullptr);
            editor.reset();

            if (newEditor != nullptr)
            {
                editor = std::move (newEditor);
                setContentNonOwned (editor.get(), true);
            }

            setResizable (editor == nullptr || editor->allowWindowResizing(), false);

            if (editor != nullptr && editor->allowWindowResizing())
                setConstrainer (editor->getBoundsConstrainer());
        }
    }

    void moved() override
    {
        if (updateStoredBounds)
        {
            plugin.windowState->lastWindowBounds = getBounds();
            plugin.edit.pluginChanged (plugin);
        }
    }

    void closeButtonPressed() override              { plugin.windowState->closeWindowExplicitly(); }
    float getDesktopScaleFactor() const override    { return 1.0f; }

    te::Plugin& plugin;
    std::unique_ptr<te::Plugin::EditorComponent> editor;
    bool updateStoredBounds = false;
};

//==============================================================================
/** Tells the engine how to open plugin windows. */
class CroftUIBehaviour : public te::UIBehaviour
{
public:
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& pws) override
    {
        if (auto ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
            return PluginWindow::create (ws->plugin);

        return {};
    }

    void recreatePluginWindowContentAsync (te::Plugin& p) override
    {
        if (auto* w = dynamic_cast<PluginWindow*> (p.windowState->pluginWindow.get()))
            return w->recreateEditorAsync();

        UIBehaviour::recreatePluginWindowContentAsync (p);
    }

    /** Used by the engine for renders (stem export). The base class does nothing, so
        without this every render silently produces no file. Runs the job on a
        background thread behind a modal progress window. */
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& job) override
    {
        struct Runner : public juce::ThreadWithProgressWindow
        {
            Runner (te::ThreadPoolJobWithProgress& j)
                : ThreadWithProgressWindow (j.getJobName(), true, false), job (j) {}

            void run() override
            {
                while (! threadShouldExit())
                {
                    auto result = job.runJob();
                    setProgress (job.getCurrentTaskProgress());

                    if (result == juce::ThreadPoolJob::jobHasFinished)
                        break;
                }
            }

            te::ThreadPoolJobWithProgress& job;
        };

        Runner (job).runThread();
    }

    void showWarningMessage (const juce::String& message) override
    {
        std::cerr << "Croft DAW: " << message << std::endl;
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Croft DAW", message);
    }
};
