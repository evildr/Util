/*
	This file is part of the Util library.
	Copyright (C) 2007-2012 Benjamin Eikel <benjamin@eikel.org>
	Copyright (C) 2007-2012 Claudius Jähn <claudius@uni-paderborn.de>
	Copyright (C) 2007-2012 Ralf Petring <ralf@petring.net>
	
	This library is subject to the terms of the Mozilla Public License, v. 2.0.
	You should have received a copy of the MPL along with this library; see the 
	file LICENSE. If not, you can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "NetworkTCP.h"
#include "../Concurrency/Concurrency.h"
#include "../Concurrency/Thread.h"
#include "../Macros.h"
#include "../Timer.h"
#include "../Utils.h"
#include <algorithm>

#ifdef UTIL_HAVE_LIB_SDL2_NET
COMPILER_WARN_PUSH
COMPILER_WARN_OFF_GCC(-Wswitch-default)
#include <SDL_net.h>
COMPILER_WARN_POP
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif
#include <cstdint>
#include <vector>

namespace Util {
namespace Network {

#ifdef UTIL_HAVE_LIB_SDL2_NET
extern IPaddress toSDLIPv4Address(const IPv4Address & address);
extern IPv4Address fromSDLIPv4Address(const IPaddress & address);
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
extern IPv4Address fromSockaddr(const sockaddr_in & sockAddr);
extern sockaddr_in toSockaddr(const IPv4Address & address);
#endif

struct TCPConnection::InternalData {
		IPv4Address remoteIp;
		float lastActiveTime;

#ifdef UTIL_HAVE_LIB_SDL2_NET
		TCPsocket tcpSocket;

		InternalData(TCPsocket && socket, const IPv4Address & address) :
			remoteIp(address), lastActiveTime(0), tcpSocket(std::forward<TCPsocket>(socket)){
		}
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
		int tcpSocket;

		InternalData(int socket, const IPv4Address & address) :
			tcpSocket(socket),
			remoteIp(address),
			lastActiveTime(0) {
		}
#endif
};

//! (static) Factory
Reference<TCPConnection> TCPConnection::connect(const IPv4Address & remoteIp) {
#ifdef UTIL_HAVE_LIB_SDL2_NET
	IPaddress sdlIp = toSDLIPv4Address(remoteIp);

	auto tcpSocket = SDLNet_TCP_Open(&sdlIp);
	if (!tcpSocket) {
		WARN(std::string("SDLNet_TCP_Open: ") + SDLNet_GetError());
		return nullptr;
	}

	return new TCPConnection(InternalData(std::move(tcpSocket), remoteIp));
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
	auto tcpSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcpSocket == -1) {
		const int error = errno;
		WARN(std::string(strerror(error)));
		return nullptr;
	}
	sockaddr_in sockAddr = toSockaddr(remoteIp);
	int result = ::connect(tcpSocket, reinterpret_cast<const sockaddr *>(&sockAddr), sizeof(sockaddr_in));
	if (result == -1) {
		const int error = errno;
		WARN(std::string(strerror(error)));
		return nullptr;
	}
	const int optionTrue = 1;
	setsockopt(tcpSocket, IPPROTO_TCP, TCP_NODELAY, &optionTrue, sizeof(int));

	return new TCPConnection(InternalData(std::move(tcpSocket), remoteIp));
#endif
}

// ----------------------------------------

//! (ctor)
TCPConnection::TCPConnection(InternalData && internalData) :
		UserThread(),
		connectionDataMutex(Concurrency::createMutex()), connectionData(new InternalData(std::forward<InternalData>(internalData))), state(OPEN), stateMutex(Concurrency::createMutex()), inQueueDataSize(0),
		inQueueMutex(Concurrency::createMutex()), outQueueMutex(Concurrency::createMutex()) {
	UserThread::start();
}

//! (dtor)
TCPConnection::~TCPConnection() {
	close();
}

float TCPConnection::getLastActiveTime() const {
	auto lock = Concurrency::createLock(*connectionDataMutex);
	return connectionData->lastActiveTime;
}

IPv4Address TCPConnection::getRemoteIp() const {
	auto lock = Concurrency::createLock(*connectionDataMutex);
	return connectionData->remoteIp;
}

bool TCPConnection::sendData(const std::vector<uint8_t> & data){
	if (!isOpen()) {
		return false;
	}
	auto lock = Concurrency::createLock(*outQueueMutex);
	outQueue.emplace_back(data);
	return true;
}

bool TCPConnection::sendString(const std::string & s) {
	return sendData(std::vector<uint8_t>(s.begin(), s.end()));
}

//! ---|> ThreadObject
void TCPConnection::run() {
	uint8_t buffer[BUFFER_SIZE];
#ifdef UTIL_HAVE_LIB_SDL2_NET
	SDLNet_SocketSet socketSet;
	{
		auto lockC = Concurrency::createLock(*connectionDataMutex);
		socketSet = SDLNet_AllocSocketSet(1);
		SDLNet_TCP_AddSocket(socketSet, connectionData->tcpSocket);
		connectionData->lastActiveTime = Timer::now();
	}
	while (isOpen()) {
		Utils::sleep(1);

		// send outgoing data
		if(!outQueue.empty()){ // this may give a wrong result, but requires no locking!
			auto lockC = Concurrency::createLock(*connectionDataMutex);
			auto lockO = Concurrency::createLock(*outQueueMutex);
			while (!outQueue.empty()) {
				std::vector<uint8_t> & data = outQueue.front();
				const int len = data.size();

				if (SDLNet_TCP_Send(connectionData->tcpSocket, reinterpret_cast<const void *>(data.data()), len) < len) {
					setState(CLOSING);
					break;
				}
				outQueue.pop_front();
			}
		}
		// receive data
		while (true) {
			if(!mayBeOpen() && !isOpen()) // double check: first one may be wrong because of missing lock.
				break;
			
			const int socketReady = SDLNet_CheckSockets(socketSet, 0);

			if (socketReady == 0) { // no data received
				break;
			} else if (socketReady < 0) {
				setState(CLOSING);
				WARN("TCP Connection error");
				break;
			}
			int bytesRecieved;
			{
				auto lockC = Concurrency::createLock(*connectionDataMutex);
				bytesRecieved = SDLNet_TCP_Recv(connectionData->tcpSocket, buffer, BUFFER_SIZE);
			}
			if (bytesRecieved <= 0) {
				setState(CLOSING);
				break;
			}
			{
				auto lockC = Concurrency::createLock(*connectionDataMutex);
				connectionData->lastActiveTime = Timer::now();
			}
			{
				auto lock = Concurrency::createLock(*inQueueMutex);
				inQueue.emplace_back(buffer, buffer + static_cast<size_t>(bytesRecieved));
				inQueueDataSize += static_cast<size_t> (bytesRecieved);
			}
		}
	}
	{
		auto lockC = Concurrency::createLock(*connectionDataMutex);
		SDLNet_FreeSocketSet(socketSet);
		SDLNet_TCP_Close(connectionData->tcpSocket);
		setState(CLOSED);
	}
// ---------------------------------------------------------
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)

	pollfd pollDescr;
	{
		auto lockC = Concurrency::createLock(*connectionDataMutex);
		pollDescr.fd = connectionData->tcpSocket;
		connectionData->lastActiveTime = Timer::now();
	}

	while (isOpen()) {
		// send outgoing data
		{
			auto lockC = Concurrency::createLock(*connectionDataMutex);
			auto lockO = Concurrency::createLock(*outQueueMutex);
			while (!outQueue.empty()) {
				const std::vector<uint8_t> & data = outQueue.front();
				const int len = data.size();
				if (send(connectionData->tcpSocket, data.data(), len, 0) < len) {
					const int error = errno;
					WARN(std::string(strerror(error)));
					setState(CLOSING);
					break;
				}
				outQueue.pop_front();
			}
		}

		// receive data
		while (isOpen()) {
			// Check if reading is possible. Wait at most 1 ms.
			pollDescr.events = POLLIN;
			pollDescr.revents = 0;
			const int result = poll(&pollDescr, 1, 1);

			if (result == 0) {
				// Socket is not ready for reading yet. Continue with writing.
				break;
			} else if (result < 0) {
				const int error = errno;
				WARN(std::string(strerror(error)));
				setState(CLOSING);
				break;
			}
			if(pollDescr.revents != POLLIN) {
				// An error event was returned, because only POLLIN was requested.
				const int error = errno;
				WARN(std::string(strerror(error)));
				setState(CLOSING);
				break;
			}
			int bytesRecieved;
			{
				auto lockC = Concurrency::createLock(*connectionDataMutex);
				bytesRecieved = recv(connectionData->tcpSocket, buffer, BUFFER_SIZE, 0);
				if(bytesRecieved == 0) {
					// Peer has shut down.
					setState(CLOSING);
					break;
				} else if (bytesRecieved < 0) {
					const int error = errno;
					WARN(std::string(strerror(error)));
					setState(CLOSING);
					break;
				}
				connectionData->lastActiveTime = Timer::now();
			}
			{
				auto lock = Concurrency::createLock(*inQueueMutex);
				inQueue.emplace_back(buffer, buffer + static_cast<size_t>(bytesRecieved));
				inQueueDataSize += static_cast<size_t> (bytesRecieved);
			}
		}
	}
	{
		auto lockC = Concurrency::createLock(*connectionDataMutex);
		if (::close(connectionData->tcpSocket) != 0) {
			const int error = errno;
			WARN(std::string(strerror(error)));
		}
		connectionData->tcpSocket = 0;
		setState(CLOSED);
	}
#endif
// ---------------------------------------------------------
}

void TCPConnection::close() {
	if(isOpen())
		setState(CLOSING);
	if(UserThread::isActive())
		UserThread::join();
}

//! (internal) \note caller needs to lock inQueue
std::vector<uint8_t> TCPConnection::extractDataFromInQueue(size_t numBytes) {
	if (inQueueDataSize < numBytes || numBytes == 0)
		return std::vector<uint8_t>();

	std::vector<uint8_t> data;
	data.reserve(numBytes);
	size_t remainingBytes = numBytes;
	size_t packetsToRemove = 0;
	for (auto it = inQueue.begin(); it != inQueue.end() && remainingBytes > 0; ++it) {
		// take full packet
		if (remainingBytes >= it->size()) {
			data.insert(data.end(), it->begin(), it->end());
			remainingBytes -= it->size();
			++packetsToRemove;
		} else { // only take remaining data
			data.insert(data.end(), it->begin(), it->begin() + remainingBytes);
			const std::vector<uint8_t> remainder(it->begin() + remainingBytes, it->end());
			*it = remainder;
			remainingBytes = 0;
		}
	}
	// remove packets from queue
	if (packetsToRemove == inQueue.size()) {
		inQueue.clear();
	} else {
		for (; packetsToRemove > 0; --packetsToRemove) {
			inQueue.pop_front();
		}
	}
	inQueueDataSize -= numBytes;
	return data;
}


std::vector<uint8_t> TCPConnection::receiveData() {
	if(inQueueDataSize>0){ // use without locking!
		auto lock = Concurrency::createLock(*inQueueMutex);
		return std::move(extractDataFromInQueue(inQueueDataSize));
	}else{
		return std::move(std::vector<uint8_t>());
	}
}


std::vector<uint8_t> TCPConnection::receiveData(size_t numBytes) {
	if(inQueueDataSize>=numBytes){ // use without locking!
		auto lock = Concurrency::createLock(*inQueueMutex);
		return std::move(extractDataFromInQueue(numBytes));
	}else{
		return std::move(std::vector<uint8_t>());
	}
}


std::string TCPConnection::receiveString(char delimiter/*='\0'*/) {
	if(inQueueDataSize==0){ // use without locking!
		return "";
	}else{
		auto lock = Concurrency::createLock(*inQueueMutex);

		// search form delimiter
		size_t pos = 0;
		bool found = false;
		for(auto package = inQueue.cbegin(); package != inQueue.cend() && !found; ++package) {
			for(const auto & byteEntry : *package) {
				if(byteEntry == static_cast<uint8_t>(delimiter)) {
					found = true;
					break;
				}
				pos++;
			}
		}

		// extract string
		std::string s;
		if (found) {
			const std::vector<uint8_t> d = extractDataFromInQueue(pos + 1);
			if (!d.empty())
				s.assign(d.begin(), d.end());
		}
		return s;
	}
}
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// [TCPServer]

struct TCPServer::InternalData {
#ifdef UTIL_HAVE_LIB_SDL2_NET
		TCPsocket serverSocket;

		InternalData(TCPsocket socket) : 
			serverSocket(std::forward<TCPsocket>(socket)) {
		}
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
		int tcpServerSocket;

		InternalData(int socket) : tcpServerSocket(socket) {
		}
#endif
};

//! (static) Factory
TCPServer * TCPServer::create(uint16_t port) {
#ifdef UTIL_HAVE_LIB_SDL2_NET
	IPaddress sdlIp;

	/// Create server-socket
	int val = SDLNet_ResolveHost(&sdlIp, nullptr, port);
	if (-1 == val) {
		WARN("Cannot resolve host name or address");
		return nullptr;
	}
	TCPsocket serverSocket = SDLNet_TCP_Open(&sdlIp);
	if (!serverSocket) {
		WARN(std::string("SDLNet_TCP_Open: ") + SDLNet_GetError());
		return nullptr;
	}

	return new TCPServer(std::move(serverSocket));
#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
	auto tcpServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (tcpServerSocket == -1) {
		const int error = errno;
		WARN(std::string(strerror(error)));
		return nullptr;
	}
	// Enable the socket to be bound to a previously used address again.
	const int optionTrue = 1;
	{
		const int result = setsockopt(tcpServerSocket, SOL_SOCKET, SO_REUSEADDR, &optionTrue, sizeof(int));
		if (result == -1) {
			const int error = errno;
			WARN(std::string(strerror(error)));
			return nullptr;
		}
	}
	{
		const int result = setsockopt(tcpServerSocket, IPPROTO_TCP, TCP_NODELAY, &optionTrue, sizeof(int));
		if (result == -1) {
			const int error = errno;
			WARN(std::string(strerror(error)));
			return nullptr;
		}
	}
	{
		sockaddr_in sockAddr;
		memset(&sockAddr, 0, sizeof(sockaddr_in));
		sockAddr.sin_family = AF_INET;
		sockAddr.sin_port = htons(port);
		sockAddr.sin_addr.s_addr = 0x00000000; // INADDR_ANY without old-style cast
		const int result = ::bind(tcpServerSocket, reinterpret_cast<const sockaddr *> (&sockAddr), sizeof(sockaddr_in));
		if (result == -1) {
			const int error = errno;
			WARN(std::string(strerror(error)));
			return nullptr;
		}
	}
	{
		const int result = listen(tcpServerSocket, 8);
		if (result == -1) {
			const int error = errno;
			WARN(std::string(strerror(error)));
			return nullptr;
		}
	}
	return new TCPServer(TCPServer::InternalData(tcpServerSocket));
#endif
}
// ----------------------------------------
//! (ctor) TCPServer
TCPServer::TCPServer(InternalData && internalData) :
		UserThread(),
		serverDataMutex(Concurrency::createMutex()), 
		serverData(new InternalData(std::forward<InternalData>(internalData))), 
		state(OPEN), 
		stateMutex(Concurrency::createMutex()), 
		queueMutex(Concurrency::createMutex()) {
	UserThread::start();
}

//! (dtor) TCPServer
TCPServer::~TCPServer() {
	close();
}


Reference<TCPConnection> TCPServer::getIncomingConnection() {
	auto lock = Concurrency::createLock(*queueMutex);
	Reference<TCPConnection> connection;
	if (!newConnectionsQueue.empty()) {
		connection = newConnectionsQueue.front();
		newConnectionsQueue.pop_front();
	}
	return connection;
}

//! ---|> UserThread
void TCPServer::run() {
#ifdef UTIL_HAVE_LIB_SDL2_NET
	while (isOpen()) {
		TCPsocket clientSocket;
		{
			auto lockS = Concurrency::createLock(*serverDataMutex);
			clientSocket = SDLNet_TCP_Accept(serverData->serverSocket);
		}
		if (clientSocket) {
			auto lockQ = Concurrency::createLock(*queueMutex);
			IPaddress * remote_sdlIp = SDLNet_TCP_GetPeerAddress(clientSocket);
			auto remoteIp = fromSDLIPv4Address(*remote_sdlIp);
			newConnectionsQueue.push_back(new TCPConnection(
				TCPConnection::InternalData(std::move(clientSocket), remoteIp)));
		} else {
			Utils::sleep(1);
		}
	}
	{
		auto lockS = Concurrency::createLock(*serverDataMutex);
		SDLNet_TCP_Close(serverData->serverSocket);
	}
// -----------------------------------------------------

#elif defined(__linux__) || defined(__unix__) || defined(ANDROID)
	pollfd set;
	{
		auto lockS = Concurrency::createLock(*serverDataMutex);
		set.fd = serverData->tcpServerSocket;
		set.events = POLLIN;
		set.revents = 0;
	}
	while (isOpen()) {
		// Check if a readable event is available (see accept(2)).
		const int result = poll(&set, 1, 5);
		if (result < 0) {
			const int error = errno;
			WARN(std::string(strerror(error)));
			setState(CLOSING);
			break;
		}
		if (result > 0) {
			if (set.revents != POLLIN) {
				const int error = errno;
				WARN(std::string(strerror(error)));
				setState(CLOSING);
				break;
			}
			auto lockS = Concurrency::createLock(*serverDataMutex);

			sockaddr_in clientAddr;
			socklen_t clientAddrSize = sizeof(sockaddr_in);
			int clientSocket = accept(serverData->tcpServerSocket, reinterpret_cast<sockaddr *> (&clientAddr), &clientAddrSize);

			auto lockQ = Concurrency::createLock(*queueMutex);
			auto remoteIp = fromSockaddr(clientAddr);
			newConnectionsQueue.push_back(new TCPConnection(
				TCPConnection::InternalData(clientSocket, remoteIp)));
		}
	}
	{
		auto lockS = Concurrency::createLock(*serverDataMutex);
		if (::close(serverData->tcpServerSocket) != 0) {
			const int error = errno;
			WARN(std::string(strerror(error)));
		}
		serverData->tcpServerSocket = 0;
	}
#endif
// -----------------------------------------------------

	setState(CLOSED);
}

void TCPServer::close() {
	{
		auto lock = Concurrency::createLock(*queueMutex);
		while (!newConnectionsQueue.empty()) {
			newConnectionsQueue.front()->close();
			newConnectionsQueue.pop_front();
		}
	}
	if (isOpen())
		setState(CLOSING);
	if(UserThread::isActive())
		UserThread::join();
}

}
}
