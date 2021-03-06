/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#pragma once

#include "utils_global.h"

#include "environment.h"

#include <QProcess>
#include <QTextCodec>

#include <functional>

QT_FORWARD_DECLARE_CLASS(QDebug)

namespace Utils {

class AbstractMacroExpander;

namespace Internal { class QtcProcessPrivate; }
class SynchronousProcessPrivate;
class CommandLine;
class Environment;

/* Result of SynchronousProcess execution */
class QTCREATOR_UTILS_EXPORT SynchronousProcessResponse
{
public:
    enum Result {
        // Finished with return code 0
        Finished,
        // Finished with return code != 0
        FinishedError,
        // Process terminated abnormally (kill)
        TerminatedAbnormally,
        // Executable could not be started
        StartFailed,
        // Hang, no output after time out
        Hang };

    void clear();

    // Helper to format an exit message.
    QString exitMessage(const QString &binary, int timeoutS) const;

    QByteArray allRawOutput() const;
    QString allOutput() const;

    QString stdOut() const;
    QString stdErr() const;

    Result result = StartFailed;
    int exitCode = -1;

    QByteArray rawStdOut;
    QByteArray rawStdErr;
    QTextCodec *codec = QTextCodec::codecForLocale();
};


class QTCREATOR_UTILS_EXPORT QtcProcess : public QProcess
{
    Q_OBJECT

public:
    QtcProcess(QObject *parent = nullptr);
    ~QtcProcess();

    void setEnvironment(const Environment &env);
    const Environment &environment() const;

    void setCommand(const CommandLine &cmdLine);
    const CommandLine &commandLine() const;

    void setUseCtrlCStub(bool enabled);
    void setLowPriority();
    void setDisableUnixTerminal();

    void start();
    void terminate();
    void interrupt();

    class QTCREATOR_UTILS_EXPORT Arguments
    {
    public:
        static Arguments createWindowsArgs(const QString &args);
        static Arguments createUnixArgs(const QStringList &args);

        QString toWindowsArgs() const;
        QStringList toUnixArgs() const;
        QString toString() const;

    private:
        QString m_windowsArgs;
        QStringList m_unixArgs;
        bool m_isWindows;
    };

    enum SplitError {
        SplitOk = 0, //! All went just fine
        BadQuoting, //! Command contains quoting errors
        FoundMeta //! Command contains complex shell constructs
    };

    //! Quote a single argument for usage in a unix shell command
    static QString quoteArgUnix(const QString &arg);
    //! Quote a single argument for usage in a shell command
    static QString quoteArg(const QString &arg, OsType osType = HostOsInfo::hostOs());
    //! Quote a single argument and append it to a shell command
    static void addArg(QString *args, const QString &arg, OsType osType = HostOsInfo::hostOs());
    //! Join an argument list into a shell command
    static QString joinArgs(const QStringList &args, OsType osType = HostOsInfo::hostOs());
    //! Prepare argument of a shell command for feeding into QProcess
    static Arguments prepareArgs(const QString &cmd, SplitError *err,
                                 OsType osType = HostOsInfo::hostOs(),
                                 const Environment *env = nullptr, const QString *pwd = nullptr,
                                 bool abortOnMeta = true);
    //! Prepare a shell command for feeding into QProcess
    static bool prepareCommand(const QString &command, const QString &arguments,
                               QString *outCmd, Arguments *outArgs, OsType osType = HostOsInfo::hostOs(),
                               const Environment *env = nullptr, const QString *pwd = nullptr);
    //! Quote and append each argument to a shell command
    static void addArgs(QString *args, const QStringList &inArgs);
    //! Append already quoted arguments to a shell command
    static void addArgs(QString *args, const QString &inArgs);
    //! Split a shell command into separate arguments. ArgIterator is usually a better choice.
    static QStringList splitArgs(const QString &cmd, OsType osType = HostOsInfo::hostOs(),
                                 bool abortOnMeta = false, SplitError *err = nullptr,
                                 const Environment *env = nullptr, const QString *pwd = nullptr);
    //! Safely replace the expandos in a shell command
    static bool expandMacros(QString *cmd, AbstractMacroExpander *mx,
                             OsType osType = HostOsInfo::hostOs());
    static QString expandMacros(const QString &str, AbstractMacroExpander *mx,
                                OsType osType = HostOsInfo::hostOs());

    /*! Iterate over arguments from a command line.
     *  Assumes that the name of the actual command is *not* part of the line.
     *  Terminates after the first command if the command line is complex.
     */
    class QTCREATOR_UTILS_EXPORT ArgIterator {
    public:
        ArgIterator(QString *str, OsType osType = HostOsInfo::hostOs())
            : m_str(str), m_osType(osType)
        {}
        //! Get the next argument. Returns false on encountering end of first command.
        bool next();
        //! True iff the argument is a plain string, possibly after unquoting.
        bool isSimple() const { return m_simple; }
        //! Return the string value of the current argument if it is simple, otherwise empty.
        QString value() const { return m_value; }
        //! Delete the last argument fetched via next() from the command line.
        void deleteArg();
        //! Insert argument into the command line after the last one fetched via next().
        //! This may be used before the first call to next() to insert at the front.
        void appendArg(const QString &str);
    private:
        QString *m_str, m_value;
        int m_pos = 0;
        int m_prev = -1;
        bool m_simple;
        OsType m_osType;
    };

    class QTCREATOR_UTILS_EXPORT ConstArgIterator {
    public:
        ConstArgIterator(const QString &str, OsType osType = HostOsInfo::hostOs())
            : m_str(str), m_ait(&m_str, osType)
        {}
        bool next() { return m_ait.next(); }
        bool isSimple() const { return m_ait.isSimple(); }
        QString value() const { return m_ait.value(); }
    private:
        QString m_str;
        ArgIterator m_ait;
    };

    static void setRemoteStartProcessHook(const std::function<void (QtcProcess &)> &hook);

    bool isSynchronous() const;
    void setSynchronous(bool on);

    void setOpenMode(OpenMode mode);

    bool stopProcess();
    bool readDataFromProcess(int timeoutS, QByteArray *stdOut, QByteArray *stdErr,
                             bool showTimeOutMessageBox);

    static QString normalizeNewlines(const QString &text);

    // Helpers to find binaries. Do not use it for other path variables
    // and file types.
    static QString locateBinary(const QString &binary);
    static QString locateBinary(const QString &path, const QString &binary);

private:
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void setupChildProcess() override;
#endif
    Internal::QtcProcessPrivate *d = nullptr;

    void setProcessEnvironment(const QProcessEnvironment &environment) = delete;
    QProcessEnvironment processEnvironment() const = delete;
};

QTCREATOR_UTILS_EXPORT QDebug operator<<(QDebug str, const SynchronousProcessResponse &);

using ExitCodeInterpreter = std::function<SynchronousProcessResponse::Result(int /*exitCode*/)>;
QTCREATOR_UTILS_EXPORT SynchronousProcessResponse::Result defaultExitCodeInterpreter(int code);

class QTCREATOR_UTILS_EXPORT SynchronousProcess : public QObject
{
    Q_OBJECT
public:
    SynchronousProcess();
    ~SynchronousProcess() override;

    /* Timeout for hanging processes (triggers after no more output
     * occurs on stderr/stdout). */
    void setTimeoutS(int timeoutS);
    int timeoutS() const;

    void setCodec(QTextCodec *c);
    QTextCodec *codec() const;

    QProcess::ProcessChannelMode processChannelMode () const;
    void setProcessChannelMode(QProcess::ProcessChannelMode m);

    bool stdOutBufferedSignalsEnabled() const;
    void setStdOutBufferedSignalsEnabled(bool);

    bool stdErrBufferedSignalsEnabled() const;
    void setStdErrBufferedSignalsEnabled(bool);

    bool timeOutMessageBoxEnabled() const;
    void setTimeOutMessageBoxEnabled(bool);

    Environment environment() const;
    void setEnvironment(const Environment &);

    void setWorkingDirectory(const QString &workingDirectory);
    QString workingDirectory() const;

    // Unix: Do not give the child process a terminal for input prompting.
    void setDisableUnixTerminal();

    void setExitCodeInterpreter(const ExitCodeInterpreter &interpreter);
    ExitCodeInterpreter exitCodeInterpreter() const;

    // Starts a nested event loop and runs the command
    SynchronousProcessResponse run(const CommandLine &cmd, const QByteArray &writeData = {});
    // Starts the command blocking the UI fully
    SynchronousProcessResponse runBlocking(const CommandLine &cmd);

signals:
    void stdOutBuffered(const QString &lines, bool firstTime);
    void stdErrBuffered(const QString &lines, bool firstTime);

public slots:
    bool terminate();

private:
    void slotTimeout();
    void finished(int exitCode, QProcess::ExitStatus e);
    void error(QProcess::ProcessError);
    void processStdOut(bool emitSignals);
    void processStdErr(bool emitSignals);

    SynchronousProcessPrivate *d;
};

} // namespace Utils
