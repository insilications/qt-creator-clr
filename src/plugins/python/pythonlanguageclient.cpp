/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "pythonlanguageclient.h"

#include "pythonconstants.h"
#include "pythonplugin.h"
#include "pythonsettings.h"
#include "pythonutils.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <languageclient/languageclientmanager.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <utils/infobar.h>
#include <utils/qtcprocess.h>
#include <utils/runextensions.h>
#include <utils/variablechooser.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

using namespace LanguageClient;
using namespace Utils;

namespace Python {
namespace Internal {

static constexpr char startPylsInfoBarId[] = "Python::StartPyls";
static constexpr char installPylsInfoBarId[] = "Python::InstallPyls";
static constexpr char enablePylsInfoBarId[] = "Python::EnablePyls";
static constexpr char installPylsTaskId[] = "Python::InstallPylsTask";

struct PythonLanguageServerState
{
    enum {
        CanNotBeInstalled,
        CanBeInstalled,
        AlreadyInstalled,
        AlreadyConfigured,
        ConfiguredButDisabled
    } state;
    FilePath pylsModulePath;
};

static QString pythonName(const FilePath &pythonPath)
{
    static QHash<FilePath, QString> nameForPython;
    if (!pythonPath.exists())
        return {};
    QString name = nameForPython.value(pythonPath);
    if (name.isEmpty()) {
        QtcProcess pythonProcess;
        pythonProcess.setTimeoutS(2);
        pythonProcess.setCommand({pythonPath, {"--version"}});
        pythonProcess.runBlocking();
        if (pythonProcess.result() != QtcProcess::FinishedWithSuccess)
            return {};
        name = pythonProcess.allOutput().trimmed();
        nameForPython[pythonPath] = name;
    }
    return name;
}

FilePath getPylsModulePath(CommandLine pylsCommand)
{
    static QMutex mutex; // protect the access to the cache
    QMutexLocker locker(&mutex);
    static QMap<FilePath, FilePath> cache;
    const FilePath &modulePath = cache.value(pylsCommand.executable());
    if (!modulePath.isEmpty())
        return modulePath;

    pylsCommand.addArg("-h");

    QtcProcess pythonProcess;
    Environment env = pythonProcess.environment();
    env.set("PYTHONVERBOSE", "x");
    pythonProcess.setEnvironment(env);
    pythonProcess.setCommand(pylsCommand);
    pythonProcess.runBlocking();

    static const QString pylsInitPattern = "(.*)"
                                           + QRegularExpression::escape(
                                               QDir::toNativeSeparators("/pylsp/__init__.py"))
                                           + '$';
    static const QRegularExpression regexCached(" matches " + pylsInitPattern,
                                                QRegularExpression::MultilineOption);
    static const QRegularExpression regexNotCached(" code object from " + pylsInitPattern,
                                                   QRegularExpression::MultilineOption);

    const QString output = pythonProcess.allOutput();
    for (const auto &regex : {regexCached, regexNotCached}) {
        const QRegularExpressionMatch result = regex.match(output);
        if (result.hasMatch()) {
            const FilePath &modulePath = FilePath::fromUserInput(result.captured(1));
            cache[pylsCommand.executable()] = modulePath;
            return modulePath;
        }
    }
    return {};
}

QList<const StdIOSettings *> configuredPythonLanguageServer()
{
    using namespace LanguageClient;
    QList<const StdIOSettings *> result;
    for (const BaseSettings *setting : LanguageClientManager::currentSettings()) {
        if (setting->m_languageFilter.isSupported("foo.py", Constants::C_PY_MIMETYPE))
            result << dynamic_cast<const StdIOSettings *>(setting);
    }
    return result;
}

static PythonLanguageServerState checkPythonLanguageServer(const FilePath &python)
{
    using namespace LanguageClient;
    const CommandLine pythonLShelpCommand(python, {"-m", "pylsp", "-h"});
    const FilePath &modulePath = getPylsModulePath(pythonLShelpCommand);
    for (const StdIOSettings *serverSetting : configuredPythonLanguageServer()) {
        if (modulePath == getPylsModulePath(serverSetting->command())) {
            return {serverSetting->m_enabled ? PythonLanguageServerState::AlreadyConfigured
                                             : PythonLanguageServerState::ConfiguredButDisabled,
                    FilePath()};
        }
    }

    QtcProcess pythonProcess;
    pythonProcess.setCommand(pythonLShelpCommand);
    pythonProcess.runBlocking();
    if (pythonProcess.allOutput().contains("Python Language Server"))
        return {PythonLanguageServerState::AlreadyInstalled, modulePath};

    pythonProcess.setCommand({python, {"-m", "pip", "-V"}});
    pythonProcess.runBlocking();
    if (pythonProcess.allOutput().startsWith("pip "))
        return {PythonLanguageServerState::CanBeInstalled, FilePath()};
    else
        return {PythonLanguageServerState::CanNotBeInstalled, FilePath()};
}

static const QStringList &plugins()
{
    static const QStringList plugins{"flake8",
                                     "jedi_completion",
                                     "jedi_definition",
                                     "jedi_hover",
                                     "jedi_references",
                                     "jedi_signature_help",
                                     "jedi_symbols",
                                     "mccabe",
                                     "pycodestyle",
                                     "pydocstyle",
                                     "pyflakes",
                                     "pylint",
                                     "rope_completion",
                                     "yapf"};
    return plugins;
}

class PylsConfigureDialog : public QDialog
{
    Q_DECLARE_TR_FUNCTIONS(PylsConfigureDialog)
public:
    PylsConfigureDialog()
        : QDialog(Core::ICore::dialogParent())
        , m_editor(jsonEditor())
        , m_advancedLabel(new QLabel)
        , m_pluginsGroup(new QGroupBox(tr("Plugins:")))
    {
        auto mainLayout = new QVBoxLayout;

        auto pluginsLayout = new QGridLayout;
        m_pluginsGroup->setLayout(pluginsLayout);
        int i = 0;
        for (const QString &plugin : plugins()) {
            auto checkBox = new QCheckBox(plugin, this);
            connect(checkBox, &QCheckBox::toggled, this, [this, plugin](bool enabled) {
                updatePluginEnabled(enabled, plugin);
            });
            m_checkBoxes[plugin] = checkBox;
            pluginsLayout->addWidget(checkBox, i / 4, i % 4);
            ++i;
        }
        mainLayout->addWidget(m_pluginsGroup);

        const QString labelText = tr(
            "For a complete list of avilable options, consult the <a "
            "href=\"https://github.com/python-lsp/python-lsp-server/blob/develop/"
            "CONFIGURATION.md\">Python LSP Server configuration documentation</a>.");

        m_advancedLabel->setText(labelText);
        m_advancedLabel->setOpenExternalLinks(true);
        mainLayout->addWidget(m_advancedLabel);
        mainLayout->addWidget(m_editor->editorWidget(), 1);

        setAdvanced(false);

        mainLayout->addStretch();

        auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);

        auto advanced = new QPushButton(tr("Advanced"));
        advanced->setCheckable(true);
        advanced->setChecked(false);
        buttons->addButton(advanced, QDialogButtonBox::ActionRole);

        connect(advanced,
                &QPushButton::toggled,
                this,
                &PylsConfigureDialog::setAdvanced);

        connect(buttons->button(QDialogButtonBox::Cancel),
                &QPushButton::clicked,
                this,
                &QDialog::reject);

        connect(buttons->button(QDialogButtonBox::Ok),
                &QPushButton::clicked,
                this,
                &QDialog::accept);

        mainLayout->addWidget(buttons);
        setLayout(mainLayout);

        resize(640, 480);
    }

    void setConfiguration(const QString &configuration)
    {
        m_editor->textDocument()->setPlainText(configuration);
        updateCheckboxes();
    }

    QString configuration() const { return m_editor->textDocument()->plainText(); }

private:
    void setAdvanced(bool advanced)
    {
        m_editor->editorWidget()->setVisible(advanced);
        m_advancedLabel->setVisible(advanced);
        m_pluginsGroup->setVisible(!advanced);
        updateCheckboxes();
    }

    void updateCheckboxes()
    {
        const QJsonDocument document = QJsonDocument::fromJson(
            m_editor->textDocument()->plainText().toUtf8());
        if (document.isObject()) {
            const QJsonObject pluginsObject
                = document.object()["pylsp"].toObject()["plugins"].toObject();
            for (const QString &plugin : plugins()) {
                auto checkBox = m_checkBoxes[plugin];
                if (!checkBox)
                    continue;
                const QJsonValue enabled = pluginsObject[plugin].toObject()["enabled"];
                if (!enabled.isBool())
                    checkBox->setCheckState(Qt::PartiallyChecked);
                else
                    checkBox->setCheckState(enabled.toBool(false) ? Qt::Checked : Qt::Unchecked);
            }
        }
    }

    void updatePluginEnabled(bool enabled, const QString &plugin)
    {
        QJsonDocument document = QJsonDocument::fromJson(
            m_editor->textDocument()->plainText().toUtf8());
        if (document.isNull())
            return;
        QJsonObject config = document.object();
        QJsonObject pylsp = config["pylsp"].toObject();
        QJsonObject plugins = pylsp["plugins"].toObject();
        QJsonObject pluginValue = plugins[plugin].toObject();
        pluginValue.insert("enabled", enabled);
        plugins.insert(plugin, pluginValue);
        pylsp.insert("plugins", plugins);
        config.insert("pylsp", pylsp);
        document.setObject(config);
        m_editor->textDocument()->setPlainText(QString::fromUtf8(document.toJson()));
    }

    QMap<QString, QCheckBox *> m_checkBoxes;
    TextEditor::BaseTextEditor *m_editor = nullptr;
    QLabel *m_advancedLabel = nullptr;
    QGroupBox *m_pluginsGroup = nullptr;
};

class PyLSSettingsWidget : public QWidget
{
    Q_DECLARE_TR_FUNCTIONS(PyLSSettingsWidget)
public:
    PyLSSettingsWidget(const PyLSSettings *settings, QWidget *parent)
        : QWidget(parent)
        , m_name(new QLineEdit(settings->m_name, this))
        , m_interpreter(new QComboBox(this))
        , m_configure(new QPushButton(tr("Configure..."), this))
        , m_configuration(settings->m_configuration)
    {
        int row = 0;
        auto *mainLayout = new QGridLayout;
        mainLayout->addWidget(new QLabel(tr("Name:")), row, 0);
        mainLayout->addWidget(m_name, row, 1);
        auto chooser = new VariableChooser(this);
        chooser->addSupportedWidget(m_name);

        mainLayout->addWidget(new QLabel(tr("Python:")), ++row, 0);
        QString settingsId = settings->interpreterId();
        if (settingsId.isEmpty())
            settingsId = PythonSettings::defaultInterpreter().id;
        updateInterpreters(PythonSettings::interpreters(), settingsId);
        mainLayout->addWidget(m_interpreter, row, 1);
        setLayout(mainLayout);

        mainLayout->addWidget(m_configure, ++row, 0);

        connect(PythonSettings::instance(),
                &PythonSettings::interpretersChanged,
                this,
                &PyLSSettingsWidget::updateInterpreters);

        connect(m_configure, &QPushButton::clicked, this, &PyLSSettingsWidget::showConfigureDialog);
    }

    void updateInterpreters(const QList<Interpreter> &interpreters, const QString &defaultId)
    {
        QString currentId = interpreterId();
        if (currentId.isEmpty())
            currentId = defaultId;
        m_interpreter->clear();
        for (const Interpreter &interpreter : interpreters) {
            if (!interpreter.command.exists())
                continue;
            const QString name = QString(interpreter.name + " (%1)")
                                     .arg(interpreter.command.toUserOutput());
            m_interpreter->addItem(name, interpreter.id);
            if (!currentId.isEmpty() && currentId == interpreter.id)
                m_interpreter->setCurrentIndex(m_interpreter->count() - 1);
        }
    }

    QString name() const { return m_name->text(); }
    QString interpreterId() const { return m_interpreter->currentData().toString(); }
    QString configuration() const { return m_configuration; }

private:
    void showConfigureDialog()
    {
        PylsConfigureDialog dialog;
        dialog.setConfiguration(m_configuration);
        if (dialog.exec() == QDialog::Accepted)
            m_configuration = dialog.configuration();
    }

    QLineEdit *m_name = nullptr;
    QComboBox *m_interpreter = nullptr;
    QPushButton *m_configure = nullptr;
    QString m_configuration;
};

PyLSSettings::PyLSSettings()
{
    m_settingsTypeId = Constants::PYLS_SETTINGS_ID;
    m_name = "Python Language Server";
    m_startBehavior = RequiresFile;
    m_languageFilter.mimeTypes = QStringList(Constants::C_PY_MIMETYPE);
    m_arguments = "-m pylsp";
    const QJsonDocument config(defaultConfiguration());
    m_configuration = QString::fromUtf8(config.toJson());
}

bool PyLSSettings::isValid() const
{
    return !m_interpreterId.isEmpty() && StdIOSettings::isValid();
}

static const char interpreterKey[] = "interpreter";

QVariantMap PyLSSettings::toMap() const
{
    QVariantMap map = StdIOSettings::toMap();
    map.insert(interpreterKey, m_interpreterId);
    return map;
}

void PyLSSettings::fromMap(const QVariantMap &map)
{
    StdIOSettings::fromMap(map);
    if (m_configuration.isEmpty()) {
        const QJsonDocument config(defaultConfiguration());
        m_configuration = QString::fromUtf8(config.toJson());
    }
    setInterpreter(map[interpreterKey].toString());
}

bool PyLSSettings::applyFromSettingsWidget(QWidget *widget)
{
    bool changed = false;
    auto pylswidget = static_cast<PyLSSettingsWidget *>(widget);

    changed |= m_name != pylswidget->name();
    m_name = pylswidget->name();

    changed |= m_interpreterId != pylswidget->interpreterId();
    setInterpreter(pylswidget->interpreterId());

    if (m_configuration != pylswidget->configuration()) {
        m_configuration = pylswidget->configuration();
        if (!changed) { // if only the settings configuration changed just send an update
            const QVector<Client *> clients = LanguageClientManager::clientForSetting(this);
            for (Client *client : clients)
                client->updateConfiguration(configuration());
        }
    }

    return changed;
}

QWidget *PyLSSettings::createSettingsWidget(QWidget *parent) const
{
    return new PyLSSettingsWidget(this, parent);
}

BaseSettings *PyLSSettings::copy() const
{
    return new PyLSSettings(*this);
}

void PyLSSettings::setInterpreter(const QString &interpreterId)
{
    m_interpreterId = interpreterId;
    if (m_interpreterId.isEmpty())
        return;
    Interpreter interpreter = Utils::findOrDefault(PythonSettings::interpreters(),
                                                   Utils::equal(&Interpreter::id, interpreterId));
    m_executable = interpreter.command;
}

Client *PyLSSettings::createClient(BaseClientInterface *interface) const
{
    return new Client(interface);
}

QJsonObject PyLSSettings::defaultConfiguration()
{
    static QJsonObject configuration;
    if (configuration.isEmpty()) {
        QJsonObject enabled;
        enabled.insert("enabled", true);
        QJsonObject disabled;
        disabled.insert("enabled", false);
        QJsonObject plugins;
        plugins.insert("flake8", disabled);
        plugins.insert("jedi_completion", enabled);
        plugins.insert("jedi_definition", enabled);
        plugins.insert("jedi_hover", enabled);
        plugins.insert("jedi_references", enabled);
        plugins.insert("jedi_signature_help", enabled);
        plugins.insert("jedi_symbols", enabled);
        plugins.insert("mccabe", disabled);
        plugins.insert("pycodestyle", disabled);
        plugins.insert("pydocstyle", disabled);
        plugins.insert("pyflakes", enabled);
        plugins.insert("pylint", disabled);
        plugins.insert("rope_completion", enabled);
        plugins.insert("yapf", enabled);
        QJsonObject pylsp;
        pylsp.insert("plugins", plugins);
        configuration.insert("pylsp", pylsp);
    }
    return configuration;
}

PyLSConfigureAssistant *PyLSConfigureAssistant::instance()
{
    static auto *instance = new PyLSConfigureAssistant(PythonPlugin::instance());
    return instance;
}

const StdIOSettings *PyLSConfigureAssistant::languageServerForPython(const FilePath &python)
{
    return findOrDefault(configuredPythonLanguageServer(),
                         [pythonModulePath = getPylsModulePath(
                              CommandLine(python, {"-m", "pylsp"}))](const StdIOSettings *setting) {
                             return getPylsModulePath(setting->command()) == pythonModulePath;
                         });
}

static Client *registerLanguageServer(const FilePath &python)
{
    Interpreter interpreter = Utils::findOrDefault(PythonSettings::interpreters(),
                                                   Utils::equal(&Interpreter::command, python));
    StdIOSettings *settings = nullptr;
    if (!interpreter.id.isEmpty()) {
        auto *pylsSettings = new PyLSSettings();
        pylsSettings->setInterpreter(interpreter.id);
        settings = pylsSettings;
    } else {
        // cannot find a matching interpreter in settings for the python path add a generic server
        auto *settings = new StdIOSettings();
        settings->m_executable = python;
        settings->m_arguments = "-m pylsp";
        settings->m_languageFilter.mimeTypes = QStringList(Constants::C_PY_MIMETYPE);
    }
    settings->m_name = PyLSConfigureAssistant::tr("Python Language Server (%1)")
                           .arg(pythonName(python));
    LanguageClientManager::registerClientSettings(settings);
    Client *client = LanguageClientManager::clientForSetting(settings).value(0);
    PyLSConfigureAssistant::updateEditorInfoBars(python, client);
    return client;
}

class PythonLSInstallHelper : public QObject
{
    Q_OBJECT
public:
    PythonLSInstallHelper(const FilePath &python, QPointer<TextEditor::TextDocument> document)
        : m_python(python)
        , m_document(document)
    {
        m_watcher.setFuture(m_future.future());
    }

    void run()
    {
        Core::ProgressManager::addTask(m_future.future(), "Install PyLS", installPylsTaskId);
        connect(&m_process, &QtcProcess::finished, this, &PythonLSInstallHelper::installFinished);
        connect(&m_process,
                &QtcProcess::readyReadStandardError,
                this,
                &PythonLSInstallHelper::errorAvailable);
        connect(&m_process,
                &QtcProcess::readyReadStandardOutput,
                this,
                &PythonLSInstallHelper::outputAvailable);

        connect(&m_killTimer, &QTimer::timeout, this, &PythonLSInstallHelper::cancel);
        connect(&m_watcher, &QFutureWatcher<void>::canceled, this, &PythonLSInstallHelper::cancel);

        QStringList arguments = {"-m", "pip", "install", "python-lsp-server[all]"};

        // add --user to global pythons, but skip it for venv pythons
        if (!QDir(m_python.parentDir().toString()).exists("activate"))
            arguments << "--user";

        m_process.setCommand({m_python, arguments});
        m_process.start();

        Core::MessageManager::writeDisrupting(
            tr("Running \"%1\" to install Python language server.")
                .arg(m_process.commandLine().toUserOutput()));

        m_killTimer.setSingleShot(true);
        m_killTimer.start(5 /*minutes*/ * 60 * 1000);
    }

private:
    void cancel()
    {
        m_process.stopProcess();
        Core::MessageManager::writeFlashing(
            tr("The Python language server installation was canceled by %1.")
                .arg(m_killTimer.isActive() ? tr("user") : tr("time out")));
    }

    void installFinished()
    {
        m_future.reportFinished();
        if (m_process.result() == QtcProcess::FinishedWithSuccess) {
            if (Client *client = registerLanguageServer(m_python))
                LanguageClientManager::openDocumentWithClient(m_document, client);
        } else {
            Core::MessageManager::writeFlashing(
                tr("Installing the Python language server failed with exit code %1")
                    .arg(m_process.exitCode()));
        }
        deleteLater();
    }

    void outputAvailable()
    {
        const QString &stdOut = QString::fromLocal8Bit(m_process.readAllStandardOutput().trimmed());
        if (!stdOut.isEmpty())
            Core::MessageManager::writeSilently(stdOut);
    }

    void errorAvailable()
    {
        const QString &stdErr = QString::fromLocal8Bit(m_process.readAllStandardError().trimmed());
        if (!stdErr.isEmpty())
            Core::MessageManager::writeSilently(stdErr);
    }

    QFutureInterface<void> m_future;
    QFutureWatcher<void> m_watcher;
    QtcProcess m_process;
    QTimer m_killTimer;
    const FilePath m_python;
    QPointer<TextEditor::TextDocument> m_document;
};

void PyLSConfigureAssistant::installPythonLanguageServer(const FilePath &python,
                                                         QPointer<TextEditor::TextDocument> document)
{
    document->infoBar()->removeInfo(installPylsInfoBarId);

    // Hide all install info bar entries for this python, but keep them in the list
    // so the language server will be setup properly after the installation is done.
    for (TextEditor::TextDocument *additionalDocument : m_infoBarEntries[python])
        additionalDocument->infoBar()->removeInfo(installPylsInfoBarId);

    auto install = new PythonLSInstallHelper(python, document);
    install->run();
}

static void setupPythonLanguageServer(const FilePath &python,
                                      QPointer<TextEditor::TextDocument> document)
{
    document->infoBar()->removeInfo(startPylsInfoBarId);
    if (Client *client = registerLanguageServer(python))
        LanguageClientManager::openDocumentWithClient(document, client);
}

static void enablePythonLanguageServer(const FilePath &python,
                                       QPointer<TextEditor::TextDocument> document)
{
    document->infoBar()->removeInfo(enablePylsInfoBarId);
    if (const StdIOSettings *setting = PyLSConfigureAssistant::languageServerForPython(python)) {
        LanguageClientManager::enableClientSettings(setting->m_id);
        if (const StdIOSettings *setting = PyLSConfigureAssistant::languageServerForPython(python)) {
            if (Client *client = LanguageClientManager::clientForSetting(setting).value(0)) {
                LanguageClientManager::openDocumentWithClient(document, client);
                PyLSConfigureAssistant::updateEditorInfoBars(python, client);
            }
        }
    }
}

void PyLSConfigureAssistant::documentOpened(Core::IDocument *document)
{
    auto textDocument = qobject_cast<TextEditor::TextDocument *>(document);
    if (!textDocument || textDocument->mimeType() != Constants::C_PY_MIMETYPE)
        return;

    const FilePath &python = detectPython(textDocument->filePath());
    if (!python.exists())
        return;

    instance()->openDocumentWithPython(python, textDocument);
}

void PyLSConfigureAssistant::openDocumentWithPython(const FilePath &python,
                                                    TextEditor::TextDocument *document)
{
    using CheckPylsWatcher = QFutureWatcher<PythonLanguageServerState>;

    QPointer<CheckPylsWatcher> watcher = new CheckPylsWatcher();

    // cancel and delete watcher after a 10 second timeout
    QTimer::singleShot(10000, this, [watcher]() {
        if (watcher) {
            watcher->cancel();
            watcher->deleteLater();
        }
    });

    connect(watcher,
            &CheckPylsWatcher::resultReadyAt,
            this,
            [=, document = QPointer<TextEditor::TextDocument>(document)]() {
                if (!document || !watcher)
                    return;
                handlePyLSState(python, watcher->result(), document);
                watcher->deleteLater();
            });
    watcher->setFuture(Utils::runAsync(&checkPythonLanguageServer, python));
}

void PyLSConfigureAssistant::handlePyLSState(const FilePath &python,
                                             const PythonLanguageServerState &state,
                                             TextEditor::TextDocument *document)
{
    if (state.state == PythonLanguageServerState::CanNotBeInstalled)
        return;
    if (state.state == PythonLanguageServerState::AlreadyConfigured) {
        if (const StdIOSettings *setting = languageServerForPython(python)) {
            if (Client *client = LanguageClientManager::clientForSetting(setting).value(0))
                LanguageClientManager::openDocumentWithClient(document, client);
        }
        return;
    }

    resetEditorInfoBar(document);
    Utils::InfoBar *infoBar = document->infoBar();
    if (state.state == PythonLanguageServerState::CanBeInstalled
        && infoBar->canInfoBeAdded(installPylsInfoBarId)) {
        auto message = tr("Install and set up Python language server (PyLS) for %1 (%2). "
                          "The language server provides Python specific completion and annotation.")
                           .arg(pythonName(python), python.toUserOutput());
        Utils::InfoBarEntry info(installPylsInfoBarId,
                                 message,
                                 Utils::InfoBarEntry::GlobalSuppression::Enabled);
        info.addCustomButton(tr("Install"),
                             [=]() { installPythonLanguageServer(python, document); });
        infoBar->addInfo(info);
        m_infoBarEntries[python] << document;
    } else if (state.state == PythonLanguageServerState::AlreadyInstalled
               && infoBar->canInfoBeAdded(startPylsInfoBarId)) {
        auto message = tr("Found a Python language server for %1 (%2). "
                          "Set it up for this document?")
                           .arg(pythonName(python), python.toUserOutput());
        Utils::InfoBarEntry info(startPylsInfoBarId,
                                 message,
                                 Utils::InfoBarEntry::GlobalSuppression::Enabled);
        info.addCustomButton(tr("Set Up"), [=]() { setupPythonLanguageServer(python, document); });
        infoBar->addInfo(info);
        m_infoBarEntries[python] << document;
    } else if (state.state == PythonLanguageServerState::ConfiguredButDisabled
               && infoBar->canInfoBeAdded(enablePylsInfoBarId)) {
        auto message = tr("Enable Python language server for %1 (%2)?")
                           .arg(pythonName(python), python.toUserOutput());
        Utils::InfoBarEntry info(enablePylsInfoBarId,
                                 message,
                                 Utils::InfoBarEntry::GlobalSuppression::Enabled);
        info.addCustomButton(tr("Enable"), [=]() { enablePythonLanguageServer(python, document); });
        infoBar->addInfo(info);
        m_infoBarEntries[python] << document;
    }
}

void PyLSConfigureAssistant::updateEditorInfoBars(const FilePath &python, Client *client)
{
    for (TextEditor::TextDocument *document : instance()->m_infoBarEntries.take(python)) {
        instance()->resetEditorInfoBar(document);
        if (client)
            LanguageClientManager::openDocumentWithClient(document, client);
    }
}

void PyLSConfigureAssistant::resetEditorInfoBar(TextEditor::TextDocument *document)
{
    for (QList<TextEditor::TextDocument *> &documents : m_infoBarEntries)
        documents.removeAll(document);
    Utils::InfoBar *infoBar = document->infoBar();
    infoBar->removeInfo(installPylsInfoBarId);
    infoBar->removeInfo(startPylsInfoBarId);
    infoBar->removeInfo(enablePylsInfoBarId);
}

PyLSConfigureAssistant::PyLSConfigureAssistant(QObject *parent)
    : QObject(parent)
{
    Core::EditorManager::instance();

    connect(Core::EditorManager::instance(),
            &Core::EditorManager::documentClosed,
            this,
            [this](Core::IDocument *document) {
                if (auto textDocument = qobject_cast<TextEditor::TextDocument *>(document))
                    resetEditorInfoBar(textDocument);
            });
}

} // namespace Internal
} // namespace Python

#include "pythonlanguageclient.moc"
