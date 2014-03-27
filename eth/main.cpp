/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file main.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 * Ethereum client.
 */

#include <ncurses.h>
#undef OK
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include "Defaults.h"
#include "Client.h"
#include "PeerNetwork.h"
#include "BlockChain.h"
#include "State.h"
#include "FileSystem.h"
#include "Instruction.h"
#include "BuildInfo.h"
using namespace std;
using namespace eth;
using eth::Instruction;
using eth::c_instructionInfo;

bool isTrue(std::string const& _m)
{
	return _m == "on" || _m == "yes" || _m == "true" || _m == "1";
}

bool isFalse(std::string const& _m)
{
	return _m == "off" || _m == "no" || _m == "false" || _m == "0";
}

void help()
{
	cout
        << "Usage eth [OPTIONS] <remote-host>" << endl
        << "Options:" << endl
        << "    -a,--address <addr>  Set the coinbase (mining payout) address to addr (default: auto)." << endl
        << "    -c,--client-name <name>  Add a name to your client's version string (default: blank)." << endl
        << "    -d,--db-path <path>  Load database from path (default:  ~/.ethereum " << endl
        << "                         <APPDATA>/Etherum or Library/Application Support/Ethereum)." << endl
        << "    -h,--help  Show this help message and exit." << endl
        << "    -i,--interactive  Enter interactive mode (default: non-interactive)." << endl
        << "    -l,--listen <port>  Listen on the given port for incoming connected (default: 30303)." << endl
		<< "    -m,--mining <on/off/number>  Enable mining, optionally for a specified number of blocks (Default: off)" << endl
        << "    -n,--upnp <on/off>  Use upnp for NAT (default: on)." << endl
        << "    -o,--mode <full/peer>  Start a full node or a peer node (Default: full)." << endl
        << "    -p,--port <port>  Connect to remote port (default: 30303)." << endl
        << "    -r,--remote <host>  Connect to remote host (default: none)." << endl
        << "    -s,--secret <secretkeyhex>  Set the secret key for use with send command (default: auto)." << endl
        << "    -u,--public-ip <ip>  Force public ip to given (default; auto)." << endl
        << "    -v,--verbosity <0 - 9>  Set the log verbosity from 0 to 9 (Default: 8)." << endl
        << "    -x,--peers <number>  Attempt to connect to given number of peers (Default: 5)." << endl
        << "    -V,--version  Show the version and exit." << endl;
        exit(0);
}

void interactiveHelp()
{
	cout
        << "Commands:" << endl
        << "    netstart <port> Starts the network sybsystem on a specific port." << endl
        << "    netstop   Stops the network subsystem." << endl
        << "    connect <addr> <port>  Connects to a specific peer." << endl
        << "    minestart  Starts mining." << endl
        << "    minestop  Stops mining." << endl
        << "    address  Gives the current address." << endl
        << "    secret  Gives the current secret" << endl
        << "    block  Gives the current block height." << endl
        << "    balance  Gives the current balance." << endl
        << "    transact <secret> <dest> <amount>  Executes a given transaction." << endl
        << "    send <dest> <amount>  Executes a given transaction with current secret." << endl
        << "    inspect <contract> Dumps a contract to <APPDATA>/<contract>.evm." << endl
        << "    exit  Exits the application." << endl;
}

void version()
{
	cout << "eth version " << ETH_QUOTED(ETH_VERSION) << endl;
	cout << "Build: " << ETH_QUOTED(ETH_BUILD_PLATFORM) << "/" << ETH_QUOTED(ETH_BUILD_TYPE) << endl;
	exit(0);
}

namespace nc {
	class nc_window_streambuf : public std::streambuf
	{
	private:
		WINDOW * m_pnl;
		unsigned long m_flags;
		std::ostream * m_os;
		std::streambuf * m_old;
		void copy( const nc_window_streambuf & rhs );
	public:
		nc_window_streambuf( WINDOW * p, std::ostream & os, unsigned long curses_attr = 0 );
		nc_window_streambuf( WINDOW * p, unsigned long curses_attr = 0 );
		nc_window_streambuf( const nc_window_streambuf & rhs );
		nc_window_streambuf & operator=( const nc_window_streambuf & rhs );
		virtual ~nc_window_streambuf();
		virtual int overflow( int c );
		virtual int sync();
	};

	nc_window_streambuf::nc_window_streambuf( WINDOW * p, unsigned long curses_attr ) : m_pnl(p), m_flags(curses_attr), m_os(0),m_old(0)
	{
		// Tell parent class that we want to call overflow() for each
		// input char:
		this->setp( 0, 0 );
		this->setg( 0, 0, 0 );
		scrollok(p, true);
		mvwinch( p, 0, 0 );
	}

	nc_window_streambuf::nc_window_streambuf( WINDOW * p, std::ostream & os, unsigned long curses_attr ) : m_pnl(p), m_flags(curses_attr), m_os(&os),m_old(os.rdbuf())
	{
		this->setp( 0, 0 );
		this->setg( 0, 0, 0 );
		os.rdbuf( this );
		scrollok(p, true);
		mvwinch( p, 0, 0 );
	}

	void nc_window_streambuf::copy( const nc_window_streambuf & rhs )
	{
		if ( this != &rhs )
		{
			this->m_pnl = rhs.m_pnl;
			this->m_flags = rhs.m_flags;
			this->m_os = rhs.m_os;
			this->m_old = rhs.m_old;
		}
	}

	nc_window_streambuf::nc_window_streambuf( const nc_window_streambuf & rhs )
	{
		this->copy(rhs);
	}

	nc_window_streambuf & nc_window_streambuf::operator=( const nc_window_streambuf & rhs )
	{
		this->copy(rhs);
		return *this;
	}

	nc_window_streambuf::~nc_window_streambuf()
	{
		if ( this->m_os )
		{
			this->m_os->rdbuf( this->m_old );
		}
	}

	int nc_window_streambuf::overflow( int c )
	{
		int ret = c;
		if ( c != EOF )
		{
			int x = 0;
			int y = 0;
			getyx( this->m_pnl, y, x);
			if (y < 1) { y = 1; }
			if (x < 2) { x = 2; }
			if ( this->m_flags )
			{
				wattron( this->m_pnl, this->m_flags );
				if( ERR == mvwaddch( this->m_pnl, y, x++, (chtype)c ) ) ret = EOF;
				wattroff( this->m_pnl, this->m_flags );
			}
			else if ( ERR == mvwaddch( this->m_pnl, y, x++, (chtype)c ) ) ret = EOF;
		}
		if ( (EOF==c) ) // || std::isspace(c) )
		{
			if ( EOF == this->sync() ) ret = EOF;
		}
		return ret;
	}

	int nc_window_streambuf::sync()
	{
		if ( stdscr && this->m_pnl )
		{
			return (ERR == wrefresh( this->m_pnl )) ? EOF : 0;
		}
		return EOF;
	}
}


int main(int argc, char** argv) {

	unsigned short listenPort = 30303;
	string remoteHost;
	unsigned short remotePort = 30303;
	bool interactive = false;
	string dbPath;
	eth::uint mining = ~(eth::uint)0;
	NodeMode mode = NodeMode::Full;
	unsigned peers = 5;
	string publicIP;
	bool upnp = true;
	string clientName;

	// Init defaults
	Defaults::get();

	// Our address.
	KeyPair us = KeyPair::create();
	Address coinbase = us.address();

	string configFile = getDataDir() + "/config.rlp";
	bytes b = contents(configFile);
	if (b.size())
	{
		RLP config(b);
		us = KeyPair(config[0].toHash<Secret>());
		coinbase = config[1].toHash<Address>();
	}
	else
	{
		RLPStream config(2);
		config << us.secret() << coinbase;
		writeFile(configFile, config.out());
	}

	for (int i = 1; i < argc; ++i)
	{
		string arg = argv[i];
		if ((arg == "-l" || arg == "--listen" || arg == "--listen-port") && i + 1 < argc)
			listenPort = (short)atoi(argv[++i]);
		else if ((arg == "-u" || arg == "--public-ip" || arg == "--public") && i + 1 < argc)
			publicIP = argv[++i];
		else if ((arg == "-r" || arg == "--remote") && i + 1 < argc)
			remoteHost = argv[++i];
		else if ((arg == "-p" || arg == "--port") && i + 1 < argc)
			remotePort = (short)atoi(argv[++i]);
		else if ((arg == "-n" || arg == "--upnp") && i + 1 < argc)
		{
			string m = argv[++i];
			if (isTrue(m))
				upnp = true;
			else if (isFalse(m))
				upnp = false;
			else
			{
				cerr << "Invalid UPnP option: " << m << endl;
				return -1;
			}
		}
		else if ((arg == "-c" || arg == "--client-name") && i + 1 < argc)
			clientName = argv[++i];
		else if ((arg == "-a" || arg == "--address" || arg == "--coinbase-address") && i + 1 < argc)
			coinbase = h160(fromHex(argv[++i]));
		else if ((arg == "-s" || arg == "--secret") && i + 1 < argc)
			us = KeyPair(h256(fromHex(argv[++i])));
		else if (arg == "-i" || arg == "--interactive")
			interactive = true;
		else if ((arg == "-d" || arg == "--path" || arg == "--db-path") && i + 1 < argc)
			dbPath = argv[++i];
		else if ((arg == "-m" || arg == "--mining") && i + 1 < argc)
		{
			string m = argv[++i];
			if (isTrue(m))
				mining = ~(eth::uint)0;
			else if (isFalse(m))
				mining = 0;
			else if (int i = stoi(m))
				mining = i;
			else
			{
				cerr << "Unknown mining option: " << m << endl;
				return -1;
			}
		}
		else if ((arg == "-v" || arg == "--verbosity") && i + 1 < argc)
			g_logVerbosity = atoi(argv[++i]);
		else if ((arg == "-x" || arg == "--peers") && i + 1 < argc)
			peers = atoi(argv[++i]);
		else if ((arg == "-o" || arg == "--mode") && i + 1 < argc)
		{
			string m = argv[++i];
			if (m == "full")
				mode = NodeMode::Full;
			else if (m == "peer")
				mode = NodeMode::PeerServer;
			else
			{
				cerr << "Unknown mode: " << m << endl;
				return -1;
			}
		}
		else if (arg == "-h" || arg == "--help")
			help();
		else if (arg == "-V" || arg == "--version")
			version();
		else
			remoteHost = argv[i];
	}

	if (!clientName.empty())
		clientName += "/";
	Client c("Ethereum(++)/" + clientName + "v" ETH_QUOTED(ETH_VERSION) "/" ETH_QUOTED(ETH_BUILD_TYPE) "/" ETH_QUOTED(ETH_BUILD_PLATFORM), coinbase, dbPath);

	if (interactive)
	{
		cout << "Ethereum (++)" << endl;
		cout << "  Code by Gav Wood, (c) 2013, 2014." << endl;
		cout << "  Based on a design by Vitalik Buterin." << endl << endl;

		/*  Initialize ncurses  */
		const char* chr;
		char* str = new char[255];
		int termwidth, termheight;
		std::string cmd;
		WINDOW * mainwin, * consolewin, * logwin, * blockswin, * pendingwin, * contractswin, * peerswin;

		if ( (mainwin = initscr()) == NULL ) {
			cerr << "Error initialising ncurses.";
			return -1;
		}

		getmaxyx(mainwin, termheight, termwidth);
		int width = termwidth, height = termheight;

		nonl();
		nocbreak();
		timeout(30000);
		echo();
		keypad(mainwin, true);

		logwin = newwin(height * 2 / 5 - 2, width, height * 3 / 5, 0);
		nc::nc_window_streambuf outbuf( logwin, std::cout );
		// nc::nc_window_streambuf errbuf( logwin, std::cerr );
		g_logVerbosity = 1; // Force verbosity level for now

		consolewin   = newwin(height * 3 / 5, width / 4, 0, 0);
		blockswin    = newwin(height * 3 / 5, width / 4, 0, width / 4);
		pendingwin   = newwin(height * 1 / 5, width / 4, 0, width * 2 / 4);
		peerswin     = newwin(height * 2 / 5, width / 4, height * 1 / 5, width * 2 / 4);
		contractswin = newwin(height * 3 / 5, width / 4, 0, width * 3 / 4);

		wsetscrreg(consolewin, 1, height * 3 / 5 - 2);
		wsetscrreg(blockswin, 1, height * 3 / 5 - 2);
		wsetscrreg(pendingwin, 1, height * 1 / 5 - 2);
		wsetscrreg(peerswin, 1, height * 2 / 5 - 2);
		wsetscrreg(contractswin, 1, height * 3 / 5 - 2);

		mvwaddnstr(consolewin, 4, 2, "Ethereum (++) " ETH_QUOTED(ETH_VERSION) "\n", width / 4 - 4);
		mvwaddnstr(consolewin, 5, 2, "  Code by Gav Wood, (c) 2013, 2014.\n", width / 4 - 4);
		mvwaddnstr(consolewin, 6, 2, "  Based on a design by Vitalik Buterin.\n", width / 4 - 4);

		mvwaddnstr(consolewin, 7, 2, "Type 'netstart 30303' to start networking", width / 4 - 4);
		mvwaddnstr(consolewin, 8, 2, "Type 'connect 54.201.28.117 30303' to connect", width / 4 - 4);
		mvwaddnstr(consolewin, 9, 2, "Type 'exit' to quit", width / 4 - 4);

		mvwprintw(mainwin, 1, 2, "> ");

		wresize(mainwin, 3, width);
		mvwin(mainwin, height - 3, 0);

		wmove(mainwin, 1, 4);

		if (!remoteHost.empty())
			c.startNetwork(listenPort, remoteHost, remotePort, mode, peers, publicIP, upnp);

		while (true)
		{
			int y = 0;

			wclrtobot(pendingwin);
			wclrtobot(peerswin);
			wclrtobot(contractswin);

			box(mainwin, 0, 0);
			box(blockswin, 0, 0);
			box(pendingwin, 0, 0);
			box(peerswin, 0, 0);
			box(consolewin, 0, 0);
			box(contractswin, 0, 0);

			mvwprintw(blockswin, 0, 2, "Blocks");
			mvwprintw(pendingwin, 0, 2, "Pending");
			mvwprintw(contractswin, 0, 2, "Contracts");

			// Block
			mvwprintw(consolewin, 0, 2, "Block # ");
			eth::uint n = c.blockChain().details().number;
			chr = toString(n).c_str();
			mvwprintw(consolewin, 0, 10, chr);

			// Address
			mvwprintw(consolewin, 1, 2, "Address: ");
			chr = toHex(us.address().asArray()).c_str();
			mvwprintw(consolewin, 2, 2, chr);

			// Balance
			mvwprintw(consolewin, height * 3 / 5 - 1, 2, "Balance: ");
			u256 balance = c.state().balance(us.address());
			chr = toString(balance).c_str();
			mvwprintw(consolewin, height * 3 / 5 - 1, 11, chr);

			// Peers
			mvwprintw(peerswin, 0, 2, "Peers: ");
			chr = toString(c.peers().size()).c_str();
			mvwprintw(peerswin, 0, 9, chr);

			// Prompt
			wmove(mainwin, 1, 4);
			getstr(str);

			string s(str);
			istringstream iss(s);
			iss >> cmd;

			mvwprintw(mainwin, 1, 2, "> ");
			clrtoeol();

			if (s.length() > 1) {
				mvwaddstr(consolewin, height * 3 / 5 - 3, 2, "> ");
				wclrtoeol(consolewin);
				mvwaddnstr(consolewin, height * 3 / 5 - 3, 4, str, width - 6);
				mvwaddch(consolewin, height * 3 / 5 - 3, width / 4 - 1, ACS_VLINE);
			}

			if (cmd == "netstart")
			{
				eth::uint port;
				iss >> port;
				c.startNetwork((short)port);
			}
			else if (cmd == "connect")
			{
				string addr;
				eth::uint port;
				iss >> addr >> port;
				c.connect(addr, (short)port);
			}
			else if (cmd == "netstop")
			{
				c.stopNetwork();
			}
			else if (cmd == "minestart")
			{
				c.startMining();
			}
			else if (cmd == "minestop")
			{
				c.stopMining();
			}
			else if (cmd == "address")
			{
				mvwaddstr(consolewin, height * 3 / 5 - 3, 2, "Current address:\n");
				mvwaddch(consolewin, height * 3 / 5 - 3, width / 4 - 1, ACS_VLINE);
				const char* addchr = toHex(us.address().asArray()).c_str();
				mvwaddstr(consolewin, height * 3 / 5 - 2, 2, addchr);
			}
			else if (cmd == "secret")
			{
				mvwaddstr(consolewin, height * 3 / 5 - 4, 2, "Current secret:\n");
				mvwaddch(consolewin, height * 3 / 5 - 3, width / 4 - 1, ACS_VLINE);
				const char* addchr = toHex(us.secret().asArray()).c_str();
				mvwaddstr(consolewin, height * 3 / 5 - 3, 2, addchr);
			}
			else if (cmd == "block")
			{
				eth::uint n = c.blockChain().details().number;
				mvwaddstr(consolewin, height * 3 / 5 - 1, 2, "Current block # ");
				const char* addchr = toString(n).c_str();
				waddstr(consolewin, addchr);
			}
			else if (cmd == "balance")
			{
				u256 balance = c.state().balance(us.address());
				mvwaddstr(consolewin, height * 3 / 5 - 1, 2, "Current balance: ");
				const char* addchr = toString(balance).c_str();
				waddstr(consolewin, addchr);
			}	
			else if (cmd == "transact")
			{
				string sechex;
				string rechex;
				u256 amount;
				u256 gasPrice;
				u256 gas;
				cin >> sechex >> rechex >> amount >> gasPrice >> gas;
				Secret secret = h256(fromHex(sechex));
				Address dest = h160(fromHex(rechex));
				bytes data;
				c.transact(secret, amount, gasPrice, dest, gas, data);
			}
			else if (cmd == "send")
			{
				string rechex;
				u256 amount;
				u256 gasPrice;
				u256 gas;
				cin >> rechex >> amount >> gasPrice >> gas;
				Address dest = h160(fromHex(rechex));

				c.transact(us.secret(), amount, gasPrice, dest, gas, bytes());
			}
			else if (cmd == "inspect")
			{
				string rechex;
				iss >> rechex;

				if (rechex.length() != 40)
				{
					cout << "Invalid address length" << endl;
				}
				else
				{
					c.lock();
					auto h = h160(fromHex(rechex));

					stringstream s;
					auto mem = c.state().contractMemory(h);
					u256 next = 0;
					unsigned numerics = 0;
					bool unexpectedNumeric = false;
					for (auto i: mem)
					{
						if (next < i.first)
						{
							unsigned j;
							for (j = 0; j <= numerics && next + j < i.first; ++j)
								s << (j < numerics || unexpectedNumeric ? " 0" : " STOP");
							unexpectedNumeric = false;
							numerics -= min(numerics, j);
							if (next + j < i.first)
								s << "\n@" << showbase << hex << i.first << "    ";
						}
						else if (!next)
						{
							s << "@" << showbase << hex << i.first << "    ";
						}
						auto iit = c_instructionInfo.find((Instruction)(unsigned)i.second);
						if (numerics || iit == c_instructionInfo.end() || (u256)(unsigned)iit->first != i.second)	// not an instruction or expecting an argument...
						{
							if (numerics)
								numerics--;
							else
								unexpectedNumeric = true;
							s << " " << showbase << hex << i.second;
						}
						else
						{
							auto const& ii = iit->second;
							s << " " << ii.name;
							numerics = ii.additional;
						}
						next = i.first + 1;
					}

					string outFile = getDataDir() + "/" + rechex + ".evm";
					ofstream ofs;
					ofs.open(outFile, ofstream::binary);
					ofs.write(s.str().c_str(), s.str().length());
					ofs.close();

					c.unlock();
				}
			}
			else if (cmd == "help")
			{
				interactiveHelp();
			}
			else if (cmd == "exit")
			{
				break;
			}

			// Clear cmd at each pass
			cmd = "";


			// Blocks
			auto const& st = c.state();
			auto const& bc = c.blockChain();
			y = 0;
			for (auto h = bc.currentHash(); h != bc.genesisHash(); h = bc.details(h).parent)
			{
				auto d = bc.details(h);
				std::string s = "# " + std::to_string(d.number) + ' ' +  toString(h); // .abridged();
				y += 1;
				mvwaddnstr(blockswin, y, 2, s.c_str(), width / 4 - 4);

				for (auto const& i: RLP(bc.block(h))[1])
				{
					Transaction t(i.data());
					std::string ss;
					ss = t.receiveAddress ?
						"  " + toString(toHex(t.safeSender().asArray())) + " " + (st.isContractAddress(t.receiveAddress) ? '*' : '-') + "> " + toString(t.receiveAddress) + ": " + toString(formatBalance(t.value)) + " [" + toString((unsigned)t.nonce) + "]":
						"  " + toString(toHex(t.safeSender().asArray())) + " +> " + toString(right160(t.sha3())) + ": " + toString(formatBalance(t.value)) + " [" + toString((unsigned)t.nonce) + "]";
					y += 1;
					mvwaddnstr(blockswin, y, 2, ss.c_str(), width / 4 - 6);
					if (y > height * 3 / 5 - 4) break;
				}
				if (y > height * 3 / 5 - 3) break;
			}


			// Pending
			y = 0;
			for (Transaction const& t: c.pending())
			{
				std::string ss;
				if (t.receiveAddress) {
					ss = toString(toHex(t.safeSender().asArray())) + " " + (st.isContractAddress(t.receiveAddress) ? '*' : '-') + "> " + toString(t.receiveAddress) + ": " + toString(formatBalance(t.value)) + " " + " [" + toString((unsigned)t.nonce) + "]";
				}
				else {
					ss = toString(toHex(t.safeSender().asArray())) + " +> " + toString(right160(t.sha3())) + ": " + toString(formatBalance(t.value)) + "[" + toString((unsigned)t.nonce) + "]";
				}
				y += 1;
				mvwaddnstr(pendingwin, y, 2, ss.c_str(), width / 4 - 6);
				if (y > height * 3 / 5 - 4) break;
			}


			// Contracts
			auto acs = st.addresses();
			y = 0;
			for (auto n = 0; n < 2; ++n)
				for (auto i: acs)
				{
					auto r = i.first;

					if (st.isContractAddress(r)) {
						std::string ss;
						ss = toString(r) + " : " + toString(formatBalance(i.second)) + " [" + toString((unsigned)st.transactionsFrom(i.first)) + "]";
						y += 1;
						mvwaddnstr(contractswin, y, 2, ss.c_str(), width / 4 - 5);
						if (y > height * 3 / 5 - 4) break;
					}
				}

			// Peers
			y = 0;
			std::string psc;
			std::string pss;
			auto cp = c.peers();
			psc = toString(cp.size()) + " peer(s)";
			for (PeerInfo const& i: cp)
			{
				pss = toString(chrono::duration_cast<chrono::milliseconds>(i.lastPing).count()) + " ms - " + i.host + ":" + toString(i.port) + " - " + i.clientVersion;
				y += 1;
				mvwaddnstr(peerswin, y, 2, pss.c_str(), width / 4 - 5);
				if (y > height * 2 / 5 - 4) break;
			}

			wrefresh(consolewin);
			wrefresh(blockswin);
			wrefresh(pendingwin);
			wrefresh(peerswin);
			wrefresh(contractswin);
			wrefresh(mainwin);
		}

		delwin(contractswin);
		delwin(peerswin);
		delwin(pendingwin);
		delwin(blockswin);
		delwin(consolewin);
		delwin(logwin);
		delwin(mainwin);
		endwin();
		refresh();
	}
	else
	{
		cout << "Address: " << endl << toHex(us.address().asArray()) << endl;
		c.startNetwork(listenPort, remoteHost, remotePort, mode, peers, publicIP, upnp);
		eth::uint n = c.blockChain().details().number;
		if (mining)
			c.startMining();
		while (true)
		{
			if (c.blockChain().details().number - n == mining)
				c.stopMining();
			this_thread::sleep_for(chrono::milliseconds(100));
		}
	}


	return 0;
}
