/*
 * VeyonCore.cpp - implementation of Veyon Core
 *
 * Copyright (c) 2006-2019 Tobias Junghans <tobydox@veyon.io>
 *
 * This file is part of Veyon - https://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <veyonconfig.h>

#include <QLocale>
#include <QTranslator>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QGroupBox>
#include <QHostAddress>
#include <QLabel>
#include <QProcessEnvironment>
#include <QSysInfo>

#include "BuiltinFeatures.h"
#include "ComputerControlInterface.h"
#include "Filesystem.h"
#include "HostAddress.h"
#include "Logger.h"
#include "NetworkObjectDirectoryManager.h"
#include "PasswordDialog.h"
#include "PlatformPluginManager.h"
#include "PlatformCoreFunctions.h"
#include "PlatformServiceCore.h"
#include "PluginManager.h"
#include "UserGroupsBackendManager.h"
#include "VeyonConfiguration.h"
#include "VncConnection.h"


VeyonCore* VeyonCore::s_instance = nullptr;


VeyonCore::VeyonCore( QCoreApplication* application, const QString& appComponentName ) :
	QObject( application ),
	m_filesystem( new Filesystem ),
	m_config( nullptr ),
	m_logger( nullptr ),
	m_authenticationCredentials( nullptr ),
	m_cryptoCore( nullptr ),
	m_pluginManager( nullptr ),
	m_platformPluginManager( nullptr ),
	m_platformPlugin( nullptr ),
	m_builtinFeatures( nullptr ),
	m_userGroupsBackendManager( nullptr ),
	m_networkObjectDirectoryManager( nullptr ),
	m_localComputerControlInterface( nullptr ),
	m_applicationName( QStringLiteral( "Veyon" ) ),
	m_authenticationKeyName(),
	m_debugging( false )
{
	Q_ASSERT( application != nullptr );

	s_instance = this;

	setupApplicationParameters();

	initPlatformPlugin();

	initConfiguration();

	initLogging( appComponentName );

	initLocaleAndTranslation();

	initCryptoCore();

	initAuthenticationCredentials();

	initPlugins();

	initManagers();

	initLocalComputerControlInterface();

	initSystemInfo();
}



VeyonCore::~VeyonCore()
{
	delete m_userGroupsBackendManager;
	m_userGroupsBackendManager = nullptr;

	delete m_authenticationCredentials;
	m_authenticationCredentials = nullptr;

	delete m_cryptoCore;
	m_cryptoCore = nullptr;

	delete m_logger;
	m_logger = nullptr;

	delete m_builtinFeatures;
	m_builtinFeatures = nullptr;

	delete m_platformPluginManager;
	m_platformPluginManager = nullptr;

	delete m_pluginManager;
	m_pluginManager = nullptr;

	delete m_config;
	m_config = nullptr;

	delete m_filesystem;
	m_filesystem = nullptr;

	s_instance = nullptr;
}



VeyonCore* VeyonCore::instance()
{
	Q_ASSERT(s_instance != nullptr);

	return s_instance;
}



QString VeyonCore::version()
{
	return QStringLiteral( VEYON_VERSION );
}



QString VeyonCore::pluginDir()
{
	return QStringLiteral( VEYON_PLUGIN_DIR );
}



QString VeyonCore::translationsDirectory()
{
	return QCoreApplication::applicationDirPath() + QDir::separator() + QStringLiteral(VEYON_TRANSLATIONS_DIR);
}



QString VeyonCore::qtTranslationsDirectory()
{
#ifdef QT_TRANSLATIONS_DIR
	return QStringLiteral( QT_TRANSLATIONS_DIR );
#else
	return translationsDirectory();
#endif
}



QString VeyonCore::executableSuffix()
{
	return QStringLiteral( VEYON_EXECUTABLE_SUFFIX ); // clazy:exclude=empty-qstringliteral
}



QString VeyonCore::sharedLibrarySuffix()
{
	return QStringLiteral( VEYON_SHARED_LIBRARY_SUFFIX );
}



QString VeyonCore::sessionIdEnvironmentVariable()
{
	return QStringLiteral("VEYON_SESSION_ID");
}



void VeyonCore::setupApplicationParameters()
{
	QCoreApplication::setOrganizationName( QStringLiteral( "Veyon Solutions" ) );
	QCoreApplication::setOrganizationDomain( QStringLiteral( "veyon.io" ) );
	QCoreApplication::setApplicationName( QStringLiteral( "Veyon" ) );

	QApplication::setAttribute( Qt::AA_UseHighDpiPixmaps );
}



bool VeyonCore::initAuthentication()
{
	switch( config().authenticationMethod() )
	{
	case AuthenticationMethod::LogonAuthentication: return initLogonAuthentication();
	case AuthenticationMethod::KeyFileAuthentication: return initKeyFileAuthentication();
	}

	return false;
}



bool VeyonCore::hasSessionId()
{
	return QProcessEnvironment::systemEnvironment().contains( sessionIdEnvironmentVariable() );
}



int VeyonCore::sessionId()
{
	return QProcessEnvironment::systemEnvironment().value( sessionIdEnvironmentVariable() ).toInt();
}



QString VeyonCore::applicationName()
{
	return instance()->m_applicationName;
}



void VeyonCore::enforceBranding( QWidget *topLevelWidget )
{
	const auto appName = QStringLiteral( "Veyon" );

	auto labels = topLevelWidget->findChildren<QLabel *>();
	for( auto label : labels )
	{
		label->setText( label->text().replace( appName, VeyonCore::applicationName() ) );
	}

	auto buttons = topLevelWidget->findChildren<QAbstractButton *>();
	for( auto button : buttons )
	{
		button->setText( button->text().replace( appName, VeyonCore::applicationName() ) );
	}

	auto groupBoxes = topLevelWidget->findChildren<QGroupBox *>();
	for( auto groupBox : groupBoxes )
	{
		groupBox->setTitle( groupBox->title().replace( appName, VeyonCore::applicationName() ) );
	}

	auto actions = topLevelWidget->findChildren<QAction *>();
	for( auto action : actions )
	{
		action->setText( action->text().replace( appName, VeyonCore::applicationName() ) );
	}

	auto widgets = topLevelWidget->findChildren<QWidget *>();
	for( auto widget : widgets )
	{
		widget->setWindowTitle( widget->windowTitle().replace( appName, VeyonCore::applicationName() ) );
	}

	topLevelWidget->setWindowTitle( topLevelWidget->windowTitle().replace( appName, VeyonCore::applicationName() ) );
}



bool VeyonCore::isDebugging()
{
	return instance()->m_debugging;
}



QByteArray VeyonCore::shortenFuncinfo( QByteArray info )
{
	const auto funcinfo = cleanupFuncinfo( info );

	if( isDebugging() )
	{
		return funcinfo + QByteArrayLiteral( "():" );
	}

	return funcinfo.split( ':' ).first() + QByteArrayLiteral(":");
}



// taken from qtbase/src/corelib/global/qlogging.cpp

QByteArray VeyonCore::cleanupFuncinfo( QByteArray info )
{
	// Strip the function info down to the base function name
	// note that this throws away the template definitions,
	// the parameter types (overloads) and any const/volatile qualifiers.

	if (info.isEmpty())
		return info;

	int pos;

	// Skip trailing [with XXX] for templates (gcc), but make
	// sure to not affect Objective-C message names.
	pos = info.size() - 1;
	if (info.endsWith(']') && !(info.startsWith('+') || info.startsWith('-'))) {
		while (--pos) {
			if (info.at(pos) == '[')
				info.truncate(pos);
		}
	}

	// operator names with '(', ')', '<', '>' in it
	static const char operator_call[] = "operator()";
	static const char operator_lessThan[] = "operator<";
	static const char operator_greaterThan[] = "operator>";
	static const char operator_lessThanEqual[] = "operator<=";
	static const char operator_greaterThanEqual[] = "operator>=";

	// canonize operator names
	info.replace("operator ", "operator");

	// remove argument list
	forever {
		int parencount = 0;
		pos = info.lastIndexOf(')');
		if (pos == -1) {
			// Don't know how to parse this function name
			return info;
		}

		// find the beginning of the argument list
		--pos;
		++parencount;
		while (pos && parencount) {
			if (info.at(pos) == ')')
				++parencount;
			else if (info.at(pos) == '(')
				--parencount;
			--pos;
		}
		if (parencount != 0)
			return info;

		info.truncate(++pos);

		if (info.at(pos - 1) == ')') {
			if (info.indexOf(operator_call) == pos - (int)strlen(operator_call))
				break;

			// this function returns a pointer to a function
			// and we matched the arguments of the return type's parameter list
			// try again
			info.remove(0, info.indexOf('('));
			info.chop(1);
			continue;
		} else {
			break;
		}
	}

	// find the beginning of the function name
	int parencount = 0;
	int templatecount = 0;
	--pos;

	// make sure special characters in operator names are kept
	if (pos > -1) {
		switch (info.at(pos)) {
		case ')':
			if (info.indexOf(operator_call) == pos - (int)strlen(operator_call) + 1)
				pos -= 2;
			break;
		case '<':
			if (info.indexOf(operator_lessThan) == pos - (int)strlen(operator_lessThan) + 1)
				--pos;
			break;
		case '>':
			if (info.indexOf(operator_greaterThan) == pos - (int)strlen(operator_greaterThan) + 1)
				--pos;
			break;
		case '=': {
			int operatorLength = (int)strlen(operator_lessThanEqual);
			if (info.indexOf(operator_lessThanEqual) == pos - operatorLength + 1)
				pos -= 2;
			else if (info.indexOf(operator_greaterThanEqual) == pos - operatorLength + 1)
				pos -= 2;
			break;
		}
		default:
			break;
		}
	}

	while (pos > -1) {
		if (parencount < 0 || templatecount < 0)
			return info;

		char c = info.at(pos);
		if (c == ')')
			++parencount;
		else if (c == '(')
			--parencount;
		else if (c == '>')
			++templatecount;
		else if (c == '<')
			--templatecount;
		else if (c == ' ' && templatecount == 0 && parencount == 0)
			break;

		--pos;
	}
	info = info.mid(pos + 1);

	// remove trailing '*', '&' that are part of the return argument
	while ((info.at(0) == '*')
		   || (info.at(0) == '&'))
		info = info.mid(1);

	// we have the full function name now.
	// clean up the templates
	while ((pos = info.lastIndexOf('>')) != -1) {
		if (!info.contains('<'))
			break;

		// find the matching close
		int end = pos;
		templatecount = 1;
		--pos;
		while (pos && templatecount) {
			char c = info.at(pos);
			if (c == '>')
				++templatecount;
			else if (c == '<')
				--templatecount;
			--pos;
		}
		++pos;
		info.remove(pos, end - pos + 1);
	}

	return info;
}



QString VeyonCore::stripDomain( const QString& username )
{
	// remove the domain part of username (e.g. "EXAMPLE.COM\Teacher" -> "Teacher")
	int domainSeparator = username.indexOf( QLatin1Char('\\') );
	if( domainSeparator >= 0 )
	{
		return username.mid( domainSeparator + 1 );
	}

	return username;
}



QString VeyonCore::formattedUuid( QUuid uuid )
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
	return uuid.toString( QUuid::WithoutBraces );
#else
	return uuid.toString().remove( QLatin1Char('{') ).remove( QLatin1Char('}') );
#endif
}



bool VeyonCore::isAuthenticationKeyNameValid( const QString& authKeyName )
{
	return QRegExp( QStringLiteral("\\w+") ).exactMatch( authKeyName );
}



int VeyonCore::exec()
{
	emit applicationLoaded();

	vDebug() << "Running";

	const auto result = QCoreApplication::instance()->exec();

	vDebug() << "Exit";

	return result;
}



void VeyonCore::initPlatformPlugin()
{
	// initialize plugin manager and load platform plugins first
	m_pluginManager = new PluginManager( this );
	m_pluginManager->loadPlatformPlugins();

	// initialize platform plugin manager and initialize used platform plugin
	m_platformPluginManager = new PlatformPluginManager( *m_pluginManager );
	m_platformPlugin = m_platformPluginManager->platformPlugin();
}



void VeyonCore::initConfiguration()
{
	m_config = new VeyonConfiguration();
	m_config->upgrade();

	if( QUuid( config().installationID() ).isNull() )
	{
		config().setInstallationID( formattedUuid( QUuid::createUuid() ) );
	}

	if( config().applicationName().isEmpty() == false )
	{
		m_applicationName = config().applicationName();
	}
}



void VeyonCore::initLogging( const QString& appComponentName )
{
	if( hasSessionId() )
	{
		m_logger = new Logger( QStringLiteral("%1-%2").arg( appComponentName ).arg( sessionId() ) );
	}
	else
	{
		m_logger = new Logger( appComponentName );
	}

	m_debugging = ( m_logger->logLevel() >= Logger::LogLevel::Debug );

	VncConnection::initLogging( isDebugging() );
}



void VeyonCore::initLocaleAndTranslation()
{
	QLocale configuredLocale( QLocale::C );

	QRegExp localeRegEx( QStringLiteral( "[^(]*\\(([^)]*)\\)") );
	if( localeRegEx.indexIn( config().uiLanguage() ) == 0 )
	{
		configuredLocale = QLocale( localeRegEx.cap( 1 ) );
	}

	if( configuredLocale.language() != QLocale::English )
	{
		auto tr = new QTranslator;
		if( configuredLocale == QLocale::C ||
			tr->load( QStringLiteral( "%1.qm" ).arg( configuredLocale.name() ), translationsDirectory() ) == false )
		{
			configuredLocale = QLocale::system(); // Flawfinder: ignore

			if( tr->load( QStringLiteral( "%1.qm" ).arg( configuredLocale.name() ), translationsDirectory() ) == false )
			{
				tr->load( QStringLiteral( "%1.qm" ).arg( configuredLocale.language() ), translationsDirectory() );
			}
		}

		QLocale::setDefault( configuredLocale );

		QCoreApplication::installTranslator( tr );

		auto qtTr = new QTranslator;
		if( qtTr->load( QStringLiteral( "qt_%1.qm" ).arg( configuredLocale.name() ), qtTranslationsDirectory() ) == false )
		{
			qtTr->load( QStringLiteral( "qt_%1.qm" ).arg( configuredLocale.language() ), qtTranslationsDirectory() );
		}

		QCoreApplication::installTranslator( qtTr );
	}

	if( configuredLocale.language() == QLocale::Hebrew ||
		configuredLocale.language() == QLocale::Arabic )
	{
		auto app = qobject_cast<QApplication *>( QCoreApplication::instance() );
		if( app )
		{
			QApplication::setLayoutDirection( Qt::RightToLeft );
		}
	}
}



void VeyonCore::initCryptoCore()
{
	m_cryptoCore = new CryptoCore;
}



void VeyonCore::initAuthenticationCredentials()
{
	if( m_authenticationCredentials )
	{
		delete m_authenticationCredentials;
		m_authenticationCredentials = nullptr;
	}

	m_authenticationCredentials = new AuthenticationCredentials;
}



void VeyonCore::initPlugins()
{
	// load all other (non-platform) plugins
	m_pluginManager->loadPlugins();
	m_pluginManager->upgradePlugins();

	m_builtinFeatures = new BuiltinFeatures();
}



void VeyonCore::initManagers()
{
	m_userGroupsBackendManager = new UserGroupsBackendManager( this );
	m_networkObjectDirectoryManager = new NetworkObjectDirectoryManager( this );
}



void VeyonCore::initLocalComputerControlInterface()
{
	const Computer localComputer( NetworkObject::Uid::createUuid(),
								  QStringLiteral("localhost"),
								  QStringLiteral("%1:%2").arg( QHostAddress( QHostAddress::LocalHost ).toString() ).arg( config().primaryServicePort() + sessionId() ) );

	m_localComputerControlInterface = new ComputerControlInterface( localComputer, this );
}



bool VeyonCore::initLogonAuthentication()
{
	if( qobject_cast<QApplication *>( QCoreApplication::instance() ) )
	{
		PasswordDialog dlg( QApplication::activeWindow() );
		if( dlg.exec() &&
			dlg.credentials().hasCredentials( AuthenticationCredentials::Type::UserLogon ) )
		{
			m_authenticationCredentials->setLogonUsername( dlg.username() );
			m_authenticationCredentials->setLogonPassword( dlg.password() );

			return true;
		}
	}

	return false;
}



bool VeyonCore::initKeyFileAuthentication()
{
	auto authKeyName = QProcessEnvironment::systemEnvironment().value( QStringLiteral("VEYON_AUTH_KEY_NAME") );

	if( authKeyName.isEmpty() == false )
	{
		if( isAuthenticationKeyNameValid( authKeyName ) &&
				m_authenticationCredentials->loadPrivateKey( VeyonCore::filesystem().privateKeyPath( authKeyName ) ) )
		{
			m_authenticationKeyName = authKeyName;
		}
	}
	else
	{
		// try to auto-detect private key file by searching for readable file
		const auto privateKeyBaseDir = VeyonCore::filesystem().expandPath( VeyonCore::config().privateKeyBaseDir() );
		const auto privateKeyDirs = QDir( privateKeyBaseDir ).entryList( QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name );

		for( const auto& privateKeyDir : privateKeyDirs )
		{
			if( m_authenticationCredentials->loadPrivateKey( VeyonCore::filesystem().privateKeyPath( privateKeyDir ) ) )
			{
				m_authenticationKeyName = privateKeyDir;
				return true;
			}
		}
	}

	return false;
}



void VeyonCore::initSystemInfo()
{
	vDebug() << version() << HostAddress::localFQDN()
			 << QSysInfo::kernelType() << QSysInfo::kernelVersion()
			 << QSysInfo::prettyProductName() << QSysInfo::productType() << QSysInfo::productVersion();
}
