/*
 * ComputerControlServer.cpp - implementation of ComputerControlServer
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

#include <QCoreApplication>

#include "AccessControlProvider.h"
#include "BuiltinFeatures.h"
#include "ComputerControlClient.h"
#include "ComputerControlServer.h"
#include "FeatureMessage.h"
#include "HostAddress.h"
#include "VeyonConfiguration.h"
#include "SystemTrayIcon.h"


ComputerControlServer::ComputerControlServer( QObject* parent ) :
	QObject( parent ),
	m_allowedIPs(),
	m_failedAuthHosts(),
	m_featureManager(),
	m_featureWorkerManager( *this, m_featureManager ),
	m_serverAuthenticationManager( this ),
	m_serverAccessControlManager( m_featureWorkerManager, VeyonCore::builtinFeatures().desktopAccessDialog(), this ),
	m_vncServer(),
	m_vncProxyServer( VeyonCore::config().localConnectOnly() || AccessControlProvider().isAccessToLocalComputerDenied() ?
						  QHostAddress::LocalHost : QHostAddress::Any,
					  VeyonCore::config().primaryServicePort() + VeyonCore::sessionId(),
					  this,
					  this )
{
	VeyonCore::builtinFeatures().systemTrayIcon().setToolTip(
				tr( "%1 Service %2 at %3:%4" ).arg( VeyonCore::applicationName(), VeyonCore::version(),
													HostAddress::localFQDN(),
													QString::number( VeyonCore::config().primaryServicePort() + VeyonCore::sessionId() ) ),
				m_featureWorkerManager );

	// make app terminate once the VNC server thread has finished
	connect( &m_vncServer, &VncServer::finished, QCoreApplication::instance(), &QCoreApplication::quit );

	connect( &m_serverAuthenticationManager, &ServerAuthenticationManager::finished,
			 this, &ComputerControlServer::showAuthenticationMessage );

	connect( &m_serverAccessControlManager, &ServerAccessControlManager::finished,
			 this, &ComputerControlServer::showAccessControlMessage );
}



ComputerControlServer::~ComputerControlServer()
{
	vDebug();

	m_vncProxyServer.stop();
}



bool ComputerControlServer::start()
{
	if( m_vncProxyServer.start( m_vncServer.serverPort(), m_vncServer.password() ) == false )
	{
		return false;
	}

	m_vncServer.prepare();
	m_vncServer.start();

	return true;
}



VncProxyConnection* ComputerControlServer::createVncProxyConnection( QTcpSocket* clientSocket,
																	 int vncServerPort,
																	 const QString& vncServerPassword,
																	 QObject* parent )
{
	return new ComputerControlClient( this, clientSocket, vncServerPort, vncServerPassword, parent );
}



bool ComputerControlServer::handleFeatureMessage( QTcpSocket* socket )
{
	char messageType;
	if( socket->getChar( &messageType ) == false )
	{
		vWarning() << "could not read feature message!";
		return false;
	}

	// receive message
	FeatureMessage featureMessage;
	if( featureMessage.isReadyForReceive( socket ) == false )
	{
		socket->ungetChar( messageType );
		return false;
	}

	featureMessage.receive( socket );

	return m_featureManager.handleFeatureMessage( *this, MessageContext( socket ), featureMessage );
}



bool ComputerControlServer::sendFeatureMessageReply( const MessageContext& context, const FeatureMessage& reply )
{
	vDebug() << reply.featureUid() << reply.command() << reply.arguments();

	char rfbMessageType = FeatureMessage::RfbMessageType;
	context.ioDevice()->write( &rfbMessageType, sizeof(rfbMessageType) );

	return reply.send( context.ioDevice() );
}



void ComputerControlServer::showAuthenticationMessage( VncServerClient* client )
{
	if( client->authState() == VncServerClient::AuthState::Failed )
	{
		vWarning() << "Authentication failed for" << client->hostAddress() << client->username();

		if( VeyonCore::config().failedAuthenticationNotificationsEnabled() )
		{
			QMutexLocker l( &m_dataMutex );

			if( m_failedAuthHosts.contains( client->hostAddress() ) == false )
			{
				m_failedAuthHosts += client->hostAddress();

				const auto fqdn = HostAddress( client->hostAddress() ).tryConvert( HostAddress::Type::FullyQualifiedDomainName );

				VeyonCore::builtinFeatures().systemTrayIcon().showMessage(
							tr( "Authentication error" ),
							tr( "User \"%1\" at host \"%2\" attempted to access this computer "
								"but could not authenticate successfully." ).arg( client->username(), fqdn ),
							m_featureWorkerManager );
			}
		}
	}
}



void ComputerControlServer::showAccessControlMessage( VncServerClient* client )
{
	if( client->accessControlState() == VncServerClient::AccessControlState::Successful )
	{
		vInfo() << "Access control successful for" << client->hostAddress() << client->username();

		if( VeyonCore::config().remoteConnectionNotificationsEnabled() )
		{
			const auto fqdn = HostAddress( client->hostAddress() ).tryConvert( HostAddress::Type::FullyQualifiedDomainName );

			VeyonCore::builtinFeatures().systemTrayIcon().showMessage(
						tr( "Remote access" ),
						tr( "User \"%1\" at host \"%2\" is now accessing this computer." ).
						arg( client->username(), fqdn ),
						m_featureWorkerManager );
		}
	}
	else if( client->accessControlState() == VncServerClient::AccessControlState::Failed )
	{
		vWarning() << "Access control failed for" << client->hostAddress() << client->username();

		if( VeyonCore::config().failedAuthenticationNotificationsEnabled() )
		{
			QMutexLocker l( &m_dataMutex );

			if( m_failedAccessControlHosts.contains( client->hostAddress() ) == false )
			{
				m_failedAccessControlHosts += client->hostAddress();

				const auto fqdn = HostAddress( client->hostAddress() ).tryConvert( HostAddress::Type::FullyQualifiedDomainName );

				VeyonCore::builtinFeatures().systemTrayIcon().showMessage(
							tr( "Access control error" ),
							tr( "User \"%1\" at host \"%2\" attempted to access this computer "
								"but has been blocked due to access control settings." ).
							arg( client->username(), fqdn ),
							m_featureWorkerManager );
			}
		}
	}
}
