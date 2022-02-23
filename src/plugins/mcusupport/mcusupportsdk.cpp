/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
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

#include "mcusupportsdk.h"
#include "mcukitmanager.h"
#include "mcupackage.h"
#include "mcusupportconstants.h"
#include "mcusupportoptions.h"
#include "mcusupportplugin.h"
#include "mcusupportversiondetection.h"
#include "mcutarget.h"
#include "mcutargetdescription.h"

#include <baremetal/baremetalconstants.h>
#include <coreplugin/icore.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainmanager.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>

#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

using namespace Utils;

namespace McuSupport {
namespace Internal {
namespace Sdk {

static FilePath findInProgramFiles(const QString &folder)
{
    for (auto envVar : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        if (!qEnvironmentVariableIsSet(envVar))
            continue;
        const FilePath dir = FilePath::fromUserInput(qEnvironmentVariable(envVar)) / folder;
        if (dir.exists())
            return dir;
    }
    return {};
}

McuPackage *createQtForMCUsPackage()
{
    return new McuPackage(McuPackage::tr("Qt for MCUs SDK"),
                          FileUtils::homePath(),                                      // defaultPath
                          FilePath("bin/qmltocpp").withExecutableSuffix(),            // detectionPath
                          Constants::SETTINGS_KEY_PACKAGE_QT_FOR_MCUS_SDK,            // settingsKey
                          QStringLiteral("Qul_DIR"));                                 // envVarName
}

static McuToolChainPackage *createMsvcToolChainPackage()
{
    return new McuToolChainPackage({}, {}, {}, {}, McuToolChainPackage::ToolChainType::MSVC);
}

static McuToolChainPackage *createGccToolChainPackage()
{
    return new McuToolChainPackage({}, {}, {}, {}, McuToolChainPackage::ToolChainType::GCC);
}

static McuToolChainPackage *createUnsupportedToolChainPackage()
{
    return new McuToolChainPackage({}, {}, {}, {}, McuToolChainPackage::ToolChainType::Unsupported);
}

static McuToolChainPackage *createArmGccPackage()
{
    const char envVar[] = "ARMGCC_DIR";

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar))
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));
    if (defaultPath.isEmpty() && HostOsInfo::isWindowsHost()) {
        const FilePath installDir = findInProgramFiles("GNU Tools ARM Embedded");
        if (installDir.exists()) {
            // If GNU Tools installation dir has only one sub dir,
            // select the sub dir, otherwise the installation dir.
            const FilePaths subDirs = installDir.dirEntries(QDir::Dirs | QDir::NoDotAndDotDot);
            if (subDirs.count() == 1)
                defaultPath = subDirs.first();
        }
    }

    const Utils::FilePath detectionPath = FilePath("bin/arm-none-eabi-g++").withExecutableSuffix();
    const auto versionDetector
        = new McuPackageExecutableVersionDetector(detectionPath,
                                                  {"--version"},
                                                  "\\b(\\d+\\.\\d+\\.\\d+)\\b");

    return new McuToolChainPackage(McuPackage::tr("GNU Arm Embedded Toolchain"),
                                   defaultPath,
                                   detectionPath,
                                   "GNUArmEmbeddedToolchain", // settingsKey
                                   McuToolChainPackage::ToolChainType::ArmGcc,
                                   envVar,
                                   versionDetector);
}

static McuToolChainPackage *createGhsToolchainPackage()
{
    const char envVar[] = "GHS_COMPILER_DIR";

    const FilePath defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));

    const auto versionDetector
        = new McuPackageExecutableVersionDetector(FilePath("as850").withExecutableSuffix(),
                                                  {"-V"},
                                                  "\\bv(\\d+\\.\\d+\\.\\d+)\\b");

    return new McuToolChainPackage("Green Hills Compiler",
                                   defaultPath,
                                   FilePath("ccv850").withExecutableSuffix(),  // detectionPath
                                   "GHSToolchain", // settingsKey
                                   McuToolChainPackage::ToolChainType::GHS,
                                   envVar,
                                   versionDetector);
}

static McuToolChainPackage *createGhsArmToolchainPackage()
{
    const char envVar[] = "GHS_ARM_COMPILER_DIR";

    const FilePath defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));

    const auto versionDetector
        = new McuPackageExecutableVersionDetector(FilePath("asarm").withExecutableSuffix(),
                                                  {"-V"},
                                                  "\\bv(\\d+\\.\\d+\\.\\d+)\\b");

    return new McuToolChainPackage("Green Hills Compiler for ARM",
                                   defaultPath,
                                   FilePath("cxarm").withExecutableSuffix(), // detectionPath
                                   "GHSArmToolchain",                                // settingsKey
                                   McuToolChainPackage::ToolChainType::GHSArm,
                                   envVar,
                                   versionDetector);
}

static McuToolChainPackage *createIarToolChainPackage()
{
    const char envVar[] = "IAR_ARM_COMPILER_DIR";

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar))
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));
    else {
        const ProjectExplorer::ToolChain *tc = ProjectExplorer::ToolChainManager::toolChain(
            [](const ProjectExplorer::ToolChain *t) {
                return t->typeId() == BareMetal::Constants::IAREW_TOOLCHAIN_TYPEID;
            });
        if (tc) {
            const FilePath compilerExecPath = tc->compilerCommand();
            defaultPath = compilerExecPath.parentDir().parentDir();
        }
    }

    const FilePath detectionPath = FilePath("bin/iccarm").withExecutableSuffix();
    const auto versionDetector
        = new McuPackageExecutableVersionDetector(detectionPath,
                                                  {"--version"},
                                                  "\\bV(\\d+\\.\\d+\\.\\d+)\\.\\d+\\b");

    return new McuToolChainPackage("IAR ARM Compiler",
                                   defaultPath,
                                   detectionPath,
                                   "IARToolchain", // settings key
                                   McuToolChainPackage::ToolChainType::IAR,
                                   envVar,
                                   versionDetector);
}

static McuPackage *createRGLPackage()
{
    const char envVar[] = "RGL_DIR";

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar)) {
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));
    } else if (Utils::HostOsInfo::isWindowsHost()) {
        const FilePath rglPath = FilePath::fromString(QDir::rootPath())
                                 / "Renesas_Electronics/D1x_RGL";
        if (rglPath.exists()) {
            defaultPath = rglPath;
            const FilePaths subDirs = defaultPath.dirEntries(
                {{"rgl_ghs_D1Mx_*"}, QDir::Dirs | QDir::NoDotAndDotDot});
            if (subDirs.count() == 1)
                defaultPath = subDirs.first();
        }
    }

    return new McuPackage("Renesas Graphics Library",
                          defaultPath,
                          {}, // detection path
                          "RGL",
                          envVar);
}

static McuPackage *createStm32CubeProgrammerPackage()
{
    FilePath defaultPath;
    const QString cubePath = "STMicroelectronics/STM32Cube/STM32CubeProgrammer";
    if (HostOsInfo::isWindowsHost()) {
        const FilePath programPath = findInProgramFiles(cubePath);
        if (!programPath.isEmpty())
            defaultPath = programPath;
    } else {
        const FilePath programPath = FileUtils::homePath() / cubePath;
        if (programPath.exists())
            defaultPath = programPath;
    }

    const FilePath detectionPath =  FilePath::fromString(
                QLatin1String(Utils::HostOsInfo::isWindowsHost()
                   ? "/bin/STM32_Programmer_CLI.exe"
                   : "/bin/STM32_Programmer.sh")
                );

    auto result
        = new McuPackage(McuPackage::tr("STM32CubeProgrammer"),
                         defaultPath,
                         detectionPath,
                         "Stm32CubeProgrammer",
                         {},                                                            // env var
                         "https://www.st.com/en/development-tools/stm32cubeprog.html", // download url
                         nullptr, // version detector
                         true, // add to path
                         "/bin" // relative path modifier
                         );
    return result;
}

static McuPackage *createMcuXpressoIdePackage()
{
    const char envVar[] = "MCUXpressoIDE_PATH";

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar)) {
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));
    } else if (HostOsInfo::isWindowsHost()) {
        const FilePath programPath = FilePath::fromString(QDir::rootPath()) / "nxp";
        if (programPath.exists()) {
            defaultPath = programPath;
            // If default dir has exactly one sub dir that could be the IDE path, pre-select that.
            const FilePaths subDirs = defaultPath.dirEntries(
                {{"MCUXpressoIDE*"}, QDir::Dirs | QDir::NoDotAndDotDot});
            if (subDirs.count() == 1)
                defaultPath = subDirs.first();
        }
    } else {
        const FilePath programPath = FilePath::fromString("/usr/local/mcuxpressoide/");
        if (programPath.exists())
            defaultPath = programPath;
    }

    return new McuPackage("MCUXpresso IDE",
                          defaultPath,
                          FilePath("ide/binaries/crt_emu_cm_redlink").withExecutableSuffix(), // detection path
                          "MCUXpressoIDE",                        // settings key
                          envVar,
                          "https://www.nxp.com/mcuxpresso/ide"); // download url
}

static McuPackage *createCypressProgrammerPackage()
{
    const char envVar[] = "CYPRESS_AUTO_FLASH_UTILITY_DIR";

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar)) {
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));
    } else if (HostOsInfo::isWindowsHost()) {
        const FilePath candidate = findInProgramFiles("Cypress");
        if (candidate.exists()) {
            // "Cypress Auto Flash Utility 1.0"
            const auto subDirs = candidate.dirEntries({{"Cypress Auto Flash Utility*"}, QDir::Dirs},
                                                      QDir::Unsorted);
            if (!subDirs.empty())
                defaultPath = subDirs.first();
        }
    }

    auto result = new McuPackage("Cypress Auto Flash Utility",
                                 defaultPath,
                                 FilePath("/bin/openocd").withExecutableSuffix(),
                                 "CypressAutoFlashUtil",
                                 envVar);
    return result;
}

static McuPackage *createRenesasProgrammerPackage()
{
    const char envVar[] = "RenesasFlashProgrammer_PATH";

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar)) {
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar));
    } else if (HostOsInfo::isWindowsHost()) {
        const FilePath candidate = findInProgramFiles("Renesas Electronics/Programming Tools");
        if (candidate.exists()) {
            // "Renesas Flash Programmer V3.09"
            const auto subDirs = candidate.dirEntries({{"Renesas Flash Programmer*"}, QDir::Dirs},
                                                      QDir::Unsorted);
            if (!subDirs.empty())
                defaultPath = subDirs.first();
        }
    }

    auto result = new McuPackage("Renesas Flash Programmer",
                                 defaultPath,
                                 FilePath("rfp-cli").withExecutableSuffix(),
                                 "RenesasFlashProgrammer",
                                 envVar);
    return result;
}
static McuPackageVersionDetector *generatePackageVersionDetector(QString envVar)
{
    if (envVar.startsWith("EVK"))
        return new McuPackageXmlVersionDetector("*_manifest_*.xml", "ksdk", "version", ".*");

    if (envVar.startsWith("STM32"))
        return new McuPackageXmlVersionDetector("package.xml",
                                                "PackDescription",
                                                "Release",
                                                "\\b(\\d+\\.\\d+\\.\\d+)\\b");

    if (envVar.startsWith("RGL"))
        return new McuPackageDirectoryVersionDetector("rgl_*_obj_*", "\\d+\\.\\d+\\.\\w+", false);

    return nullptr;
}

/// Create the McuPackage by checking the "boardSdk" property in the JSON file for the board.
/// The name of the environment variable pointing to the the SDK for the board will be defined in the "envVar" property
/// inside the "boardSdk".
static McuPackage *createBoardSdkPackage(const McuTargetDescription &desc)
{
    const auto generateSdkName = [](const QString &envVar) {
        auto postfixPos = envVar.indexOf("_SDK_PATH");
        if (postfixPos < 0) {
            postfixPos = envVar.indexOf("_DIR");
        }
        auto sdkName = postfixPos > 0 ? envVar.left(postfixPos) : envVar;
        return QString::fromLatin1("MCU SDK (%1)").arg(sdkName);
    };
    const QString sdkName = desc.boardSdk.name.isEmpty() ? generateSdkName(desc.boardSdk.envVar)
                                                         : desc.boardSdk.name;

    const FilePath defaultPath = [&] {
        const auto envVar = desc.boardSdk.envVar.toLatin1();
        if (qEnvironmentVariableIsSet(envVar))
            return FilePath::fromUserInput(qEnvironmentVariable(envVar));
        if (!desc.boardSdk.defaultPath.isEmpty()) {
            FilePath defaultPath = FilePath::fromUserInput(QDir::rootPath()
                                                           + desc.boardSdk.defaultPath);
            if (defaultPath.exists())
                return defaultPath;
        }
        return FilePath();
    }();

    const auto versionDetector = generatePackageVersionDetector(desc.boardSdk.envVar);

    return new McuPackage(sdkName,
                          defaultPath,
                          {},                   // detection path
                          desc.boardSdk.envVar, // settings key
                          desc.boardSdk.envVar, // env var
                          {},                   // download URL
                          versionDetector);
}

static McuPackage *createFreeRTOSSourcesPackage(const QString &envVar,
                                                const FilePath &boardSdkDir,
                                                const QString &freeRTOSBoardSdkSubDir)
{
    const QString envVarPrefix = envVar.chopped(int(strlen("_FREERTOS_DIR")));

    FilePath defaultPath;
    if (qEnvironmentVariableIsSet(envVar.toLatin1()))
        defaultPath = FilePath::fromUserInput(qEnvironmentVariable(envVar.toLatin1()));
    else if (!boardSdkDir.isEmpty() && !freeRTOSBoardSdkSubDir.isEmpty())
        defaultPath = boardSdkDir / freeRTOSBoardSdkSubDir;

    return new McuPackage(QString::fromLatin1("FreeRTOS Sources (%1)").arg(envVarPrefix),
                          defaultPath,
                          {},
                          QString::fromLatin1("FreeRTOSSourcePackage_%1").arg(envVarPrefix),
                          envVar,
                          "https://freertos.org");
}

struct McuTargetFactory
{
    McuTargetFactory(const QHash<QString, McuToolChainPackage *> &tcPkgs,
                     const QHash<QString, McuPackage *> &vendorPkgs)
        : tcPkgs(tcPkgs)
        , vendorPkgs(vendorPkgs)
    {}

    QVector<McuTarget *> createTargets(const McuTargetDescription &description)
    {
        auto qulVersion = QVersionNumber::fromString(description.qulVersion);
        if (qulVersion <= QVersionNumber({1, 3})) {
            if (description.platform.type == McuTargetDescription::TargetType::Desktop)
                return createDesktopTargetsLegacy(description);

            // There was a platform backends related refactoring in Qul 1.4
            // This requires different processing of McuTargetDescriptions
            return createMcuTargetsLegacy(description);
        }
        return createTargetsImpl(description);
    }

    QVector<McuAbstractPackage *> getMcuPackages() const
    {
        QVector<McuAbstractPackage *> packages;
        for (auto *package : qAsConst(boardSdkPkgs))
            packages.append(package);
        for (auto *package : qAsConst(freeRTOSPkgs))
            packages.append(package);
        return packages;
    }

protected:
    // Implementation for Qul version <= 1.3
    QVector<McuTarget *> createMcuTargetsLegacy(const McuTargetDescription &desc)
    {
        QVector<McuTarget *> mcuTargets;
        McuToolChainPackage *tcPkg = tcPkgs.value(desc.toolchain.id);
        if (!tcPkg)
            tcPkg = createUnsupportedToolChainPackage();
        for (auto os : {McuTarget::OS::BareMetal, McuTarget::OS::FreeRTOS}) {
            for (int colorDepth : desc.platform.colorDepths) {
                QVector<McuAbstractPackage *> required3rdPartyPkgs = {tcPkg};
                if (vendorPkgs.contains(desc.platform.vendor))
                    required3rdPartyPkgs.push_back(vendorPkgs.value(desc.platform.vendor));

                FilePath boardSdkDefaultPath;
                if (!desc.boardSdk.envVar.isEmpty()) {
                    if (!boardSdkPkgs.contains(desc.boardSdk.envVar)) {
                        auto boardSdkPkg = desc.boardSdk.envVar != "RGL_DIR"
                                               ? createBoardSdkPackage(desc)
                                               : createRGLPackage();
                        boardSdkPkgs.insert(desc.boardSdk.envVar, boardSdkPkg);
                    }
                    auto boardSdkPkg = boardSdkPkgs.value(desc.boardSdk.envVar);
                    boardSdkDefaultPath = boardSdkPkg->defaultPath();
                    required3rdPartyPkgs.append(boardSdkPkg);
                }
                if (os == McuTarget::OS::FreeRTOS) {
                    if (desc.freeRTOS.envVar.isEmpty()) {
                        continue;
                    } else {
                        if (!freeRTOSPkgs.contains(desc.freeRTOS.envVar)) {
                            freeRTOSPkgs
                                .insert(desc.freeRTOS.envVar,
                                        createFreeRTOSSourcesPackage(desc.freeRTOS.envVar,
                                                                     boardSdkDefaultPath,
                                                                     desc.freeRTOS.boardSdkSubDir));
                        }
                        required3rdPartyPkgs.append(freeRTOSPkgs.value(desc.freeRTOS.envVar));
                    }
                }

                const auto platform = McuTarget::Platform{desc.platform.id,
                                                          desc.platform.name,
                                                          desc.platform.vendor};
                auto mcuTarget = new McuTarget(QVersionNumber::fromString(desc.qulVersion),
                                               platform,
                                               os,
                                               required3rdPartyPkgs,
                                               tcPkg,
                                               desc.platform.colorDepths.count() > 1
                                                   ? colorDepth
                                                   : McuTarget::UnspecifiedColorDepth);
                mcuTargets.append(mcuTarget);
            }
        }
        return mcuTargets;
    }

    QVector<McuTarget *> createDesktopTargetsLegacy(const McuTargetDescription &desc)
    {
        McuToolChainPackage *tcPkg = tcPkgs.value(desc.toolchain.id);
        if (!tcPkg)
            tcPkg = createUnsupportedToolChainPackage();
        const auto platform = McuTarget::Platform{desc.platform.id,
                                                  desc.platform.name,
                                                  desc.platform.vendor};
        auto desktopTarget = new McuTarget(QVersionNumber::fromString(desc.qulVersion),
                                           platform,
                                           McuTarget::OS::Desktop,
                                           {},
                                           tcPkg);
        return {desktopTarget};
    }

    QVector<McuTarget *> createTargetsImpl(const McuTargetDescription &desc)
    {
        // OS deduction
        const auto os = [&] {
            if (desc.platform.type == McuTargetDescription::TargetType::Desktop)
                return McuTarget::OS::Desktop;
            else if (!desc.freeRTOS.envVar.isEmpty())
                return McuTarget::OS::FreeRTOS;
            return McuTarget::OS::BareMetal;
        }();

        QVector<McuTarget *> mcuTargets;
        McuToolChainPackage *tcPkg = tcPkgs.value(desc.toolchain.id);
        if (tcPkg)
            tcPkg->setVersions(desc.toolchain.versions);
        else
            tcPkg = createUnsupportedToolChainPackage();
        for (int colorDepth : desc.platform.colorDepths) {
            QVector<McuAbstractPackage *> required3rdPartyPkgs;
            // Desktop toolchains don't need any additional settings
            if (tcPkg && !tcPkg->isDesktopToolchain()
                && tcPkg->toolchainType() != McuToolChainPackage::ToolChainType::Unsupported)
                required3rdPartyPkgs.append(tcPkg);

            // Add setting specific to platform IDE
            if (vendorPkgs.contains(desc.platform.vendor))
                required3rdPartyPkgs.push_back(vendorPkgs.value(desc.platform.vendor));

            // Board SDK specific settings
            FilePath boardSdkDefaultPath;
            if (!desc.boardSdk.envVar.isEmpty()) {
                if (!boardSdkPkgs.contains(desc.boardSdk.envVar)) {
                    auto boardSdkPkg = createBoardSdkPackage(desc);
                    boardSdkPkgs.insert(desc.boardSdk.envVar, boardSdkPkg);
                }
                auto boardSdkPkg = boardSdkPkgs.value(desc.boardSdk.envVar);
                boardSdkPkg->setVersions(desc.boardSdk.versions);
                boardSdkDefaultPath = boardSdkPkg->defaultPath();
                required3rdPartyPkgs.append(boardSdkPkg);
            }

            // Free RTOS specific settings
            if (!desc.freeRTOS.envVar.isEmpty()) {
                if (!freeRTOSPkgs.contains(desc.freeRTOS.envVar)) {
                    freeRTOSPkgs.insert(desc.freeRTOS.envVar,
                                        createFreeRTOSSourcesPackage(desc.freeRTOS.envVar,
                                                                     boardSdkDefaultPath,
                                                                     desc.freeRTOS.boardSdkSubDir));
                }
                required3rdPartyPkgs.append(freeRTOSPkgs.value(desc.freeRTOS.envVar));
            }

            const McuTarget::Platform platform(
                {desc.platform.id, desc.platform.name, desc.platform.vendor});
            auto mcuTarget = new McuTarget(QVersionNumber::fromString(desc.qulVersion),
                                           platform,
                                           os,
                                           required3rdPartyPkgs,
                                           tcPkg,
                                           colorDepth);
            mcuTargets.append(mcuTarget);
        }
        return mcuTargets;
    }

private:
    const QHash<QString, McuToolChainPackage *> &tcPkgs;
    const QHash<QString, McuPackage *> &vendorPkgs;

    QHash<QString, McuPackage *> boardSdkPkgs;
    QHash<QString, McuPackage *> freeRTOSPkgs;
}; // struct McuTargetFactory

QVector<McuTarget *> targetsFromDescriptions(const QList<McuTargetDescription> &descriptions,
                                             QVector<McuAbstractPackage *> *packages)
{
    const QHash<QString, McuToolChainPackage *> tcPkgs = {
        {{"armgcc"}, createArmGccPackage()},
        {{"greenhills"}, createGhsToolchainPackage()},
        {{"iar"}, createIarToolChainPackage()},
        {{"msvc"}, createMsvcToolChainPackage()},
        {{"gcc"}, createGccToolChainPackage()},
        {{"arm-greenhills"}, createGhsArmToolchainPackage()},
    };

    // Note: the vendor name (the key of the hash) is case-sensitive. It has to match the "platformVendor" key in the
    // json file.
    const QHash<QString, McuPackage *> vendorPkgs = {
        {{"ST"}, createStm32CubeProgrammerPackage()},
        {{"NXP"}, createMcuXpressoIdePackage()},
        {{"CYPRESS"}, createCypressProgrammerPackage()},
        {{"RENESAS"}, createRenesasProgrammerPackage()},
    };

    McuTargetFactory targetFactory(tcPkgs, vendorPkgs);
    QVector<McuTarget *> mcuTargets;

    for (const auto &desc : descriptions) {
        auto newTargets = targetFactory.createTargets(desc);
        mcuTargets.append(newTargets);
    }

    packages->append(
        Utils::transform<QVector<McuAbstractPackage *>>(tcPkgs.values(),
                                                        [&](McuToolChainPackage *tcPkg) {
                                                            return tcPkg;
                                                        }));
    for (auto *package : vendorPkgs)
        packages->append(package);
    packages->append(targetFactory.getMcuPackages());

    return mcuTargets;
}

Utils::FilePath kitsPath(const Utils::FilePath &dir)
{
    return dir / "kits/";
}

static QFileInfoList targetDescriptionFiles(const Utils::FilePath &dir)
{
    const QDir kitsDir(kitsPath(dir).toString(), "*.json");
    return kitsDir.entryInfoList();
}

McuTargetDescription parseDescriptionJson(const QByteArray &data)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    const QJsonObject target = document.object();
    const QString qulVersion = target.value("qulVersion").toString();
    const QJsonObject platform = target.value("platform").toObject();
    const QString compatVersion = target.value("compatVersion").toString();
    const QJsonObject toolchain = target.value("toolchain").toObject();
    const QJsonObject boardSdk = target.value("boardSdk").toObject();
    const QJsonObject freeRTOS = target.value("freeRTOS").toObject();

    const QVariantList toolchainVersions = toolchain.value("versions").toArray().toVariantList();
    const auto toolchainVersionsList = Utils::transform<QStringList>(toolchainVersions,
                                                                     [&](const QVariant &version) {
                                                                         return version.toString();
                                                                     });
    const QVariantList boardSdkVersions = boardSdk.value("versions").toArray().toVariantList();
    const auto boardSdkVersionsList = Utils::transform<QStringList>(boardSdkVersions,
                                                                    [&](const QVariant &version) {
                                                                        return version.toString();
                                                                    });

    const QVariantList colorDepths = platform.value("colorDepths").toArray().toVariantList();
    const auto colorDepthsVector = Utils::transform<QVector<int>>(colorDepths,
                                                                  [&](const QVariant &colorDepth) {
                                                                      return colorDepth.toInt();
                                                                  });
    const QString platformName = platform.value("platformName").toString();

    return {qulVersion,
            compatVersion,
            {
                platform.value("id").toString(),
                platformName,
                platform.value("vendor").toString(),
                colorDepthsVector,
                platformName == "Desktop" ? McuTargetDescription::TargetType::Desktop
                                          : McuTargetDescription::TargetType::MCU,
            },
            {
                toolchain.value("id").toString(),
                toolchainVersionsList,
            },
            {
                boardSdk.value("name").toString(),
                boardSdk.value("defaultPath").toString(),
                boardSdk.value("envVar").toString(),
                boardSdkVersionsList,
            },
            {
                freeRTOS.value("envVar").toString(),
                freeRTOS.value("boardSdkSubDir").toString(),
            }};
}

// https://doc.qt.io/qtcreator/creator-developing-mcu.html#supported-qt-for-mcus-sdks
static const QString legacySupportVersionFor(const QString &sdkVersion)
{
    static const QHash<QString, QString> oldSdkQtcRequiredVersion = {
        {{"1.0"}, {"4.11.x"}},
        {{"1.1"}, {"4.12.0 or 4.12.1"}},
        {{"1.2"}, {"4.12.2 or 4.12.3"}}
    };
    if (oldSdkQtcRequiredVersion.contains(sdkVersion))
        return oldSdkQtcRequiredVersion.value(sdkVersion);

    if (QVersionNumber::fromString(sdkVersion).majorVersion() == 1)
        return "4.12.4 up to 6.0";

    return QString();
}


bool checkDeprecatedSdkError(const Utils::FilePath &qulDir, QString &message)
{
    const McuPackagePathVersionDetector versionDetector("(?<=\\bQtMCUs.)(\\d+\\.\\d+)");
    const QString sdkDetectedVersion = versionDetector.parseVersion(qulDir.toString());
    const QString legacyVersion = legacySupportVersionFor(sdkDetectedVersion);

    if (!legacyVersion.isEmpty()) {
        message = McuTarget::tr("Qt for MCUs SDK version %1 detected, "
                                "only supported by Qt Creator version %2. "
                                "This version of Qt Creator requires Qt for MCUs %3 or greater.")
                      .arg(sdkDetectedVersion,
                           legacyVersion,
                           McuSupportOptions::minimalQulVersion().toString());
        return true;
    }

    return false;
}

void targetsAndPackages(const Utils::FilePath &dir, McuSdkRepository *repo)
{
    QList<McuTargetDescription> descriptions;

    auto descriptionFiles = targetDescriptionFiles(dir);
    for (const QFileInfo &fileInfo : descriptionFiles) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QFile::ReadOnly))
            continue;
        const McuTargetDescription desc = parseDescriptionJson(file.readAll());
        const auto pth = Utils::FilePath::fromString(fileInfo.filePath());
        bool ok = false;
        const int compatVersion = desc.compatVersion.toInt(&ok);
        if (!desc.compatVersion.isEmpty() && ok && compatVersion > MAX_COMPATIBILITY_VERSION) {
            printMessage(McuTarget::tr("Skipped %1. Unsupported version \"%2\".")
                             .arg(QDir::toNativeSeparators(pth.fileNameWithPathComponents(1)),
                                  desc.qulVersion),
                         false);
            continue;
        }
        if (QVersionNumber::fromString(desc.qulVersion) < McuSupportOptions::minimalQulVersion()) {
            const QString legacyVersion = legacySupportVersionFor(desc.qulVersion);
            const QString qtcSupportText
                = !legacyVersion.isEmpty()
                      ? McuTarget::tr("Detected version \"%1\", only supported by Qt Creator %2.")
                            .arg(desc.qulVersion, legacyVersion)
                      : McuTarget::tr("Unsupported version \"%1\".").arg(desc.qulVersion);
            printMessage(McuTarget::tr("Skipped %1. %2 Qt for MCUs version >= %3 required.")
                             .arg(QDir::toNativeSeparators(pth.fileNameWithPathComponents(1)),
                                  qtcSupportText,
                                  McuSupportOptions::minimalQulVersion().toString()),
                         false);
            continue;
        }
        descriptions.append(desc);
    }

    // No valid description means invalid or old SDK installation.
    if (descriptions.empty()) {
        if (kitsPath(dir).exists()) {
            printMessage(McuTarget::tr("No valid kit descriptions found at %1.")
                             .arg(kitsPath(dir).toUserOutput()),
                         true);
            return;
        } else {
            QString deprecationMessage;
            if (checkDeprecatedSdkError(dir, deprecationMessage)) {
                printMessage(deprecationMessage, true);
                return;
            }
        }
    }

    repo->mcuTargets.append(targetsFromDescriptions(descriptions, &(repo->packages)));

    // Keep targets sorted lexicographically
    std::sort(repo->mcuTargets.begin(),
              repo->mcuTargets.end(),
              [](const McuTarget *lhs, const McuTarget *rhs) {
                  return McuKitManager::kitName(lhs) < McuKitManager::kitName(rhs);
              });
}

FilePath packagePathFromSettings(const QString &settingsKey,
                                 QSettings::Scope scope,
                                 const FilePath &defaultPath)
{
    QSettings *settings = Core::ICore::settings(scope);
    const QString key = QLatin1String(Constants::SETTINGS_GROUP) + '/'
                        + QLatin1String(Constants::SETTINGS_KEY_PACKAGE_PREFIX) + settingsKey;
    const QString path = settings->value(key, defaultPath.toString()).toString();
    return FilePath::fromUserInput(path);
}

} // namespace Sdk
} // namespace Internal
} // namespace McuSupport
