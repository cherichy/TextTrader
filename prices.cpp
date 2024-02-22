#include <iostream>
#include <float.h>
#include <math.h>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <stdlib.h>
#include <string.h>
// #define PDC_FORCE_UTF8
// #define PDC_WIDE
#include <curses.h>
#ifdef _WIN32
#undef getch
#undef ungetch
#include <conio.h>
#endif

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <numeric>


#include "ThostFtdcMdApi.h"

// 控制键定义
#define KEYBOARD_UP 19 // 72
#define KEYBOARD_DOWN 20 // 80
#define KEYBOARD_LEFT 21 //75
#define KEYBOARD_RIGHT 22 // 77
#define KEYBOARD_REFRESH 28 // F5


#define CONNECTION_STATUS_DISCONNECTED	0
#define CONNECTION_STATUS_CONNECTED		1
#define CONNECTION_STATUS_LOGINOK		2
#define CONNECTION_STATUS_LOGINFAILED	3

std::vector<CThostFtdcDepthMarketDataField> vquotes;
std::map<std::string, size_t> mquotes; // index quotes id ==> vquotes idx
int ConnectionStatus = CONNECTION_STATUS_DISCONNECTED;
char** instruments = NULL; // quote list to be subscribed
size_t instrument_count = 0;
std::string user,password;

#define PRECISION 2

// Basic
void init_screen();
int on_key_pressed(int ch);
void time_thread();
void work_thread();
void HandleTickTimeout();

// Main Board
void refresh_screen();
void display_title();
void display_status();
void display_quotation(const char* InstrumentID);
int on_key_pressed_mainboard(int ch);
int move_forward_1_line();
int move_backward_1_line();
int scroll_left_1_column();
int scroll_right_1_column();
void focus_quotation(int index);


void post_task(std::function<void()> task);


// CTP
class CCTPMdSpiImp :public CThostFtdcMdSpi
{
public:
	CCTPMdSpiImp(CThostFtdcMdApi* pMdApi) :m_pMdApi(pMdApi) {}

	//已连接
	void OnFrontConnected() { post_task(std::bind(&CCTPMdSpiImp::HandleFrontConnected, this)); }

	//未连接
	void OnFrontDisconnected(int nReason) { post_task(std::bind(&CCTPMdSpiImp::HandleFrontDisconnected, this, nReason)); }

	//登录应答
	void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) { post_task(std::bind(&CCTPMdSpiImp::HandleRspUserLogin, this, *pRspUserLogin, *pRspInfo, nRequestID, bIsLast)); }

	//行情服务的深度行情通知
	void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) { post_task(std::bind(&CCTPMdSpiImp::HandleRtnDepthMarketData, this, *pDepthMarketData)); }

	void HandleFrontConnected()
	{
		ConnectionStatus = CONNECTION_STATUS_CONNECTED;
		CThostFtdcReqUserLoginField Req;

		memset(&Req, 0x00, sizeof(Req));
		strncpy(Req.UserID,user.c_str(),sizeof(Req.UserID)-1);
		strncpy(Req.Password,password.c_str(),sizeof(Req.Password)-1);
		m_pMdApi->ReqUserLogin(&Req, 0);
	}

	void HandleFrontDisconnected(int nReason)
	{
		ConnectionStatus = CONNECTION_STATUS_DISCONNECTED;
		refresh_screen();
	}

	void HandleRspUserLogin(CThostFtdcRspUserLoginField& RspUserLogin, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
	{
		ConnectionStatus = CONNECTION_STATUS_LOGINOK;
		display_status();

		if (instrument_count == 0)
		{
			char* contracts[1];
			contracts[0] = (char*)"*";
			m_pMdApi->SubscribeMarketData(contracts, 1);
		}
		else
		{
			m_pMdApi->SubscribeMarketData(instruments, instrument_count);
		}
	}

	void HandleRtnDepthMarketData(CThostFtdcDepthMarketDataField& DepthMarketData)
	{
		if (!mquotes.contains(DepthMarketData.InstrumentID)) {
			// new
			vquotes.push_back(DepthMarketData);
			mquotes[DepthMarketData.InstrumentID] = vquotes.size() - 1;
		}else {
			// update
			vquotes[mquotes[DepthMarketData.InstrumentID]]=DepthMarketData;
		}
		display_quotation(DepthMarketData.InstrumentID);
	}

	CThostFtdcMdApi* m_pMdApi;
};


class semaphore
{
public:
	semaphore(int value = 1) :count(value) {}

	void wait()
	{
		std::unique_lock<std::mutex> lck(mt);
		if (--count < 0)//资源不足挂起线程
			cv.wait(lck);
	}

	void signal()
	{
		std::unique_lock<std::mutex> lck(mt);
		if (++count <= 0)//有线程挂起，唤醒一个
			cv.notify_one();
	}

private:
	int count;
	std::mutex mt;
	std::condition_variable cv;
};


std::mutex _lock;
semaphore _sem;
std::vector< std::function<void()> > _vTasks;

// typedef struct {
// 	char name[30];
// 	int width;
// } column_item_t;

// const column_item_t column_items[]={
// #define SYMBOL			0		// 合约
// 	{"合约",		20},
// #define SYMBOL_NAME		1		// 名称
// 	{"名称",		20},
// #define CLOSE			2		// 现价
// 	{"现价",		10},
// #define PERCENT			3		// 涨幅
// 	{"涨幅",		10},
// #define VOLUME			4		// 总量
// 	{"总量",		10},
// #define TRADE_VOLUME	5		// 现量
// 	{"现量",		10},
// #define ADVANCE			6		// 涨跌
// 	{"涨跌",		10},
// #define OPEN			7		// 开盘
// 	{"开盘",		10},
// #define HIGH			8		// 最高
// 	{"最高",		10},
// #define LOW				9		// 最低
// 	{"最低",		10},
// #define BID_PRICE		10		// 买价
// 	{"买价",		10},
// #define BID_VOLUME		11		// 买量
// 	{"买量",		10},
// #define ASK_PRICE		12		// 卖价
// 	{"卖价",		10},
// #define ASK_VOLUME		13		// 卖量
// 	{"卖量",		10},
// #define PREV_SETTLEMENT	14		// 昨结
// 	{"昨结",		10},
// #define SETTLEMENT		15		// 今结
// 	{"今结",		10},
// #define PREV_CLOSE		16		// 昨收
// 	{"昨收",		10},
// #define OPENINT			17		// 今仓
// 	{"今仓",		10},
// #define PREV_OPENINT	18		// 昨仓
// 	{"昨仓",		10},
// #define AVERAGE_PRICE	19		// 均价
// 	{"均价",		10},
// #define HIGH_LIMIT		20		// 涨停
// 	{"涨停",		10},
// #define LOW_LIMIT		21		// 跌停
// 	{"跌停",		10},
// #define EXCHANGE		22		// 交易所
// 	{"交易所",		10},
// #define TRADINGDAY		23		// 交易日
// 	{"交易日",		10}
// };

// convert column_items into enums
enum class COL_ITEMS{
	SYMBOL = 0,
	SYMBOL_NAME,
	CLOSE,
	PERCENT,
	VOLUME,
	// TRADE_VOLUME,
	ADVANCE,
	OPEN,
	HIGH,
	LOW,
	BID_PRICE,
	BID_VOLUME,
	ASK_PRICE,
	ASK_VOLUME,
	PREV_SETTLEMENT,
	SETTLEMENT,
	PREV_CLOSE,
	OPENINT,
	PREV_OPENINT,
	// AVERAGE_PRICE,
	HIGH_LIMIT,
	LOW_LIMIT,
	EXCHANGE,
	TRADINGDAY
};


const std::vector<std::string> column_name{
	"合约",		// SYMBOL
	"名称",		// SYMBOL_NAME
	"现价",		// CLOSE
	"涨幅",		// PERCENT
	"总量",		// VOLUME
	// "现量",		// TRADE_VOLUME
	"涨跌",		// ADVANCE
	"开盘",		// OPEN
	"最高",		// HIGH
	"最低",		// LOW
	"买价",		// BID_PRICE
	"买量",		// BID_VOLUME
	"卖价",		// ASK_PRICE
	"卖量",		// ASK_VOLUME
	"昨结",		// PREV_SETTLEMENT
	"今结",		// SETTLEMENT
	"昨收",		// PREV_CLOSE
	"今仓",		// OPENINT
	"昨仓",		// PREV_OPENINT
	// "均价",		// AVERAGE_PRICE
	"涨停",		// HIGH_LIMIT
	"跌停",		// LOW_LIMIT
	"交易所",		// EXCHANGE
	"交易日",		// TRADINGDAY
};

std::vector<int> column_width;
std::vector<bool> mcolumns;

// Mainboard Curses
int curr_line=0,curr_col=1,max_lines,max_cols=7;
int curr_pos=0,curr_col_pos=2;

#if !(defined(WIN32) || defined(_WIN32) || defined(WIN64))
#include <unistd.h>
#else
int opterr = 1, optind = 1, optopt, optreset;
char* optarg;
int getopt(int argc, char* argv[], const char* ostr)
{
	static char* place = (char*)"";		/* option letter processing */
	const char* oli;				/* option letter list index */

	if (optreset || !*place) {		/* update scanning pointer */
		optreset = 0;
		if (optind >= argc || *(place = argv[optind]) != '-') {
			place = (char*)"";
			return (EOF);
		}
		if (place[1] && *++place == '-') {	/* found "--" */
			++optind;
			place = (char*)"";
			return (EOF);
		}
	}					/* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
		!(oli = strchr(ostr, optopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means EOF.
		 */
		if (optopt == (int)'-')
			return (EOF);
		if (!*place)
			++optind;
		return ('?');
	}
	if (*++oli != ':') {			/* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			optarg = place;
		else if (argc <= ++optind) {	/* no arg */
			place = (char*)"";
			if (*ostr == ':')
				return (':');
			return ('?');
		}
		else				/* white space */
			optarg = argv[optind];
		place = (char*)"";
		++optind;
	}
	return (optopt);			/* dump back option letter */
}
#endif

void display_usage()
{
	std::cout << "usage:prices [-u user] [-p password] marketaddr instrument1,instrument2 ..." << std::endl;
	std::cout << "example:prices -u 000001 -p 888888 tcp://180.168.146.187:10131 rb2205,IF2201" << std::endl;
	std::cout << "example:prices tcp://180.168.146.187:10131 rb2205,IF2201" << std::endl;
}

int main(int argc, char * argv[])
{
	setlocale(LC_ALL, "");
	int opt;
	while ((opt = getopt(argc, argv, "u:p:")) != -1)
	{
		switch (opt) 
		{
		case 'u':
		       user = optarg;
		       break;
		case 'p':
		       password = optarg;
		       break;
		case '?':
			display_usage();
			return -1;
		}
	}

	if (argc - optind != 2)
	{
		display_usage();
		return -1;
	}

	char* servaddr = argv[optind];
	const char* p;
	
	// instruments
	instrument_count =1;
	for (p = argv[optind+1]; *p != '\0'; p++)
		if (*p == ',')
			instrument_count++;
	
	instruments = (char**)malloc(sizeof(char*)* instrument_count);
	char** instrument = instruments;
	char* token=NULL;
	for (int i=0; i<instrument_count; i++) {
		if (token == NULL) {
			token = strtok(argv[optind + 1], ",");
		}
		else {
			token = strtok(NULL, ",");
		}
		*instrument++ = token;
	}

	if (strstr(servaddr,"://") == NULL) {
		std::cout << "invalid address format" << std::endl;
		return -1;
	}
	CThostFtdcMdApi* pMdApi = CThostFtdcMdApi::CreateFtdcMdApi();
	CCTPMdSpiImp Spi(pMdApi);
	pMdApi->RegisterSpi(&Spi);
	pMdApi->RegisterFront(servaddr);
	pMdApi->Init();

	column_width = std::vector<int>(column_name.size(),10);
	column_width[0] = 20; // SYMBOL
	column_width[1] = 20; // SYMBOL_NAME

	mcolumns = std::vector<bool> (column_name.size(),true);	// column select status
	mcolumns[1] = false; // SYMBOL_NAME

	std::thread workthread(&work_thread);
	std::thread timerthread(&time_thread);

	// Idle
	int ch;
	while (1) {
#ifdef _WIN32
		ch = _getch();
#else
		ch = getchar();
#endif
		if (ch == 0 || ch == 224) {
#ifdef _WIN32
			ch = _getch();
#else
			ch = getchar();
#endif
			switch (ch)
			{	
				case 12: // ^L（刷新）
					ch = KEYBOARD_REFRESH;
					break;
				case 75: // 左
					ch = KEYBOARD_LEFT;
					break;
				case 80: // 下
					ch = KEYBOARD_DOWN;
					break;
				case 72: // 上
					ch = KEYBOARD_UP;
					break;
				case 77: // 右
					ch = KEYBOARD_RIGHT;
					break;
				case 59: // F1
				case 60: // F2
				case 61: // F3
				case 62: // F4
					break;
				case 63: // F5
					ch = KEYBOARD_REFRESH;
					break;
				case 64: // F6
				case 65: // F7
				case 66: // F8
				case 67: // F9
				case 68: // F10
					break;
				default:
					break;
			}
		}
		else {
			switch (ch)
			{
			
			default:
				break;
			}
		}
		post_task(std::bind(on_key_pressed, ch));
	}

	return 0;
}

void post_task(std::function<void()> task)
{
	_lock.lock();
	_vTasks.push_back(task);
	_lock.unlock();
	_sem.signal();
}

void HandleTickTimeout()
{
	display_status();
	refresh();
}

int move_forward_1_line()
{
	if(vquotes.size()==0)
		return 0;
	display_title();
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(curr_line==vquotes.size()-curr_pos)	// Already bottom
		return 0;
	if(curr_line!=max_lines){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line++;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		move(1,0);
		setscrreg(1,max_lines);
		scroll(stdscr);
		setscrreg(0,max_lines+1);
		curr_pos++;
		display_quotation(vquotes[curr_pos+max_lines-1].InstrumentID);	// new line
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}

int move_backward_1_line()
{
	if(vquotes.size()==0)
		return 0;
	display_title();
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(curr_line==1 && curr_pos==0)	// Already top
		return 0;
	if(curr_line>1){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line--;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		move(1,0);
		setscrreg(1,max_lines);
		scrl(-1);
		setscrreg(0,max_lines+1);
		curr_pos--;
		display_quotation(vquotes[curr_pos].InstrumentID);
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}

int scroll_left_1_column()
{
	int i;
	
	if(curr_col_pos==2)
		return 0;
	while(mcolumns[--curr_col_pos]==false); //	取消所在列的反白显示
	display_title();
	if(vquotes.size()==0)
		return 0;
	for(i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
		display_quotation(vquotes[i].InstrumentID);
	
	return 0;
}

int scroll_right_1_column()
{
	int i;
	
	if(curr_col_pos == column_name.size()-1)
		return 0;
	while(mcolumns[++curr_col_pos]==false); //	取消所在列的反白显示
	display_title();
	if(vquotes.size()==0)
		return 0;
	for(i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
		display_quotation(vquotes[i].InstrumentID);

	return 0;
}


void focus_quotation(int index)
{
	if(vquotes.size()==0)
		return;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(index>curr_pos+max_lines-1){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=max_lines;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		int i,n;
		
		n=index-(curr_pos+max_lines-1);
		for(i=0;i<n;i++)
			move_forward_1_line();
	}else if(index<curr_pos){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		int i,n;
		
		n=curr_pos-index;
		for(i=0;i<n;i++)
			move_backward_1_line();
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=index-curr_pos+1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
}

void display_quotation(const char *InstrumentID)
{
	int i,y,x,pos,maxy,maxx;

	getmaxyx(stdscr,maxy,maxx);
	i = mquotes[InstrumentID];
	if(i<curr_pos || i>curr_pos+max_lines-1)
		return;
	y=i-curr_pos+1;
	x=0;

	move(y,0);
	clrtoeol();

	double PreClosePrice = vquotes[i].PreClosePrice;
	if (PreClosePrice == DBL_MAX || fabs(PreClosePrice) < 0.000001)
		PreClosePrice = vquotes[i].PreSettlementPrice;

	for(int iter = 0,pos=0;iter<column_name.size();iter++,pos++){
		if(mcolumns[iter]==false)
			continue;
		if(iter!= (int)COL_ITEMS::SYMBOL && iter!=(int)COL_ITEMS::CLOSE && pos<curr_col_pos)
			continue;
		if(maxx-x<column_width[iter])
			break;
		switch(COL_ITEMS(iter)){
		case COL_ITEMS::SYMBOL:		//InstrumentID
			mvprintw(y,x,"%-*s",column_width[iter],vquotes[i].InstrumentID);
			x+=column_width[iter];
			break;
		case COL_ITEMS::SYMBOL_NAME:		//product_name
			// mvprintw(y,x,"%-*s",column_width[iter],vquotes[i].product_name);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::CLOSE:		//close
			if(vquotes[i].LastPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
			mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].LastPrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::PERCENT:		//close
			if (PreClosePrice == DBL_MAX || fabs(PreClosePrice) < 0.000001 || vquotes[i].LastPrice == DBL_MAX || fabs(vquotes[i].LastPrice) < 0.000001)
				mvprintw(y, x, "%*c", column_width[iter], '-');
			else
				mvprintw(y, x, "%*.1f%%", column_width[iter] - 1, (vquotes[i].LastPrice - PreClosePrice) / PreClosePrice * 100.0);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::ADVANCE:		//close
			if(vquotes[i].PreSettlementPrice==DBL_MAX || vquotes[i].PreSettlementPrice==0 || vquotes[i].LastPrice==DBL_MAX || vquotes[i].LastPrice==0)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else if(vquotes[i].LastPrice>vquotes[i].PreSettlementPrice)
				mvprintw(y,x,"%*.*f",column_width[iter]-1,PRECISION,vquotes[i].LastPrice-vquotes[i].PreSettlementPrice);
			else
				mvprintw(y,x,"%*.*f",column_width[iter]-1,PRECISION,vquotes[i].LastPrice-vquotes[i].PreSettlementPrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::VOLUME:		//volume
			mvprintw(y,x,"%*d",column_width[iter],vquotes[i].Volume);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::BID_PRICE:		//bid price
			if(vquotes[i].BidPrice1==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].BidPrice1);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::BID_VOLUME:		//bi volume
			mvprintw(y,x,"%*d",column_width[iter],vquotes[i].BidVolume1);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::ASK_PRICE:		//ask price
			if(vquotes[i].AskPrice1==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].AskPrice1);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::ASK_VOLUME:		//ask volume
			mvprintw(y,x,"%*d",column_width[iter],vquotes[i].AskVolume1);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::OPEN:		//open
			if(vquotes[i].OpenPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].OpenPrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::PREV_SETTLEMENT:		//settlement
			if(vquotes[i].PreSettlementPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].PreSettlementPrice);
			x+=column_width[iter]+1;
			break;
		// case COL_ITEMS::TRADE_VOLUME:		//volume
		// 	mvprintw(y,x,"%*d",column_width[iter],vquotes[i].trade_volume);
		// 	x+=column_width[iter]+1;
		// 	break;
		// case COL_ITEMS::AVERAGE_PRICE:		//avgprice
		// 	if(vquotes[i].average_price==DBL_MAX)
		// 		mvprintw(y,x,"%*c",column_width[iter],'-');
		// 	else
		// 		mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].average_price);
		// 	x+=column_width[iter]+1;
		// 	break;
		case COL_ITEMS::HIGH:		//high
			if(vquotes[i].HighestPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].HighestPrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::LOW:		//low
			if(vquotes[i].LowestPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].LowestPrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::HIGH_LIMIT:		//highlimit
			if (vquotes[i].UpperLimitPrice == DBL_MAX)
				mvprintw(y, x, "%*c", column_width[iter], '-');
			else
				mvprintw(y, x, "%*.*f", column_width[iter], PRECISION, vquotes[i].UpperLimitPrice);
			x += column_width[iter] + 1;
			break;
		case COL_ITEMS::LOW_LIMIT:		//lowlimit
			if (vquotes[i].LowerLimitPrice == DBL_MAX)
				mvprintw(y, x, "%*c", column_width[iter], '-');
			else
				mvprintw(y, x, "%*.*f", column_width[iter], PRECISION, vquotes[i].LowerLimitPrice);
			x += column_width[iter] + 1;
			break;
		case COL_ITEMS::SETTLEMENT:		//settlement
			if(vquotes[i].SettlementPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].SettlementPrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::PREV_CLOSE:		//preclose
			if(vquotes[i].PreClosePrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_width[iter],'-');
			else
				mvprintw(y,x,"%*.*f",column_width[iter],PRECISION,vquotes[i].PreClosePrice);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::OPENINT:		//openint
			mvprintw(y,x,"%*d",column_width[iter],vquotes[i].OpenInterest);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::PREV_OPENINT:		//previous openint
			mvprintw(y,x,"%*d",column_width[iter],vquotes[i].PreOpenInterest);
			x+=column_width[iter]+1;
			break;
		case COL_ITEMS::EXCHANGE:		//exchange
			mvprintw(y, x, "%-*s", column_width[iter], vquotes[i].ExchangeID);
			x += column_width[iter] + 1;
			break;
		case COL_ITEMS::TRADINGDAY:		//tradingday
			mvprintw(y, x, "%-*s", column_width[iter], vquotes[i].TradingDay);
			x += column_width[iter] + 1;
			break;
		default:
			break;
		}
	}

	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}

void display_status()
{
	int y,x;
	std::string status;
	char tradetime[20]={0};

	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (ConnectionStatus)
	{
		case CONNECTION_STATUS_DISCONNECTED:
		case CONNECTION_STATUS_CONNECTED:
		case CONNECTION_STATUS_LOGINFAILED:
			status = "断线";
			break;
		case CONNECTION_STATUS_LOGINOK:
			status = "在线";
			break;
		default:
			status = "";
			break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%d]",curr_pos+curr_line,vquotes.size());
	mvprintw(y-1,x-35,"cherichy  %s  %s", status.c_str(),tradetime);
}

void init_screen()
{
	int i,y,x;

	initscr();
	cbreak();
	nodelay(stdscr,TRUE);
	keypad(stdscr,TRUE);
	noecho();
	curs_set(0);
	scrollok(stdscr,TRUE);
	clear();
	getmaxyx(stdscr,y,x);
	max_lines=y-2;
	display_title();
	for(i=0;i<vquotes.size();i++)
		display_quotation(vquotes[i].InstrumentID);
	display_status();
	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}

void refresh_screen()
{
	int i,y,x;

	endwin();
	initscr();
	cbreak();
	nodelay(stdscr,TRUE);
	keypad(stdscr,TRUE);
	noecho();
	curs_set(0);
	scrollok(stdscr,TRUE);
	clear();
	getmaxyx(stdscr,y,x);
	max_lines=y-2;
	display_title();
	for(i=0;i<vquotes.size();i++)
		display_quotation(vquotes[i].InstrumentID);
	display_status();
	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}


void display_title()
{
	int y,x,pos,maxy,maxx;

	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	for(int iter = 0,pos=0;iter<column_name.size();iter++,pos++){
		if(mcolumns[iter]==false)
			continue;
		if(iter!=(int)COL_ITEMS::SYMBOL && iter!=(int)COL_ITEMS::CLOSE && pos<curr_col_pos)
			continue;
		if(maxx-x<column_width[iter])
			break;
		switch(COL_ITEMS(iter)){
			case COL_ITEMS::SYMBOL:
				mvprintw(y,x,"%-*s",column_width[iter],column_name[iter].c_str());
				x+=column_width[iter];
				break;
			case COL_ITEMS::SYMBOL_NAME:
			case COL_ITEMS::EXCHANGE:
			case COL_ITEMS::TRADINGDAY:
				mvprintw(y,x,"%-*s",column_width[iter],column_name[iter].c_str());
				x+=column_width[iter]+1;
				break;
			default:
				mvprintw(y,x,"%*s",column_width[iter]+2,column_name[iter].c_str());
				x+=column_width[iter]+1;
				break;
		}
	}
}

void work_thread()
{
	// Init screen
	init_screen();
	
	// Run
	while (true) {
		_sem.wait();
		_lock.lock();
		auto task = _vTasks.begin();
		if (task == _vTasks.end()) {
			_lock.unlock();
			continue;
		}
		(*task)();
		_vTasks.erase(task);
		_lock.unlock();
	}

	// End screen
	endwin();
}

void time_thread()
{
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		post_task(std::bind(HandleTickTimeout));
	}
}

int on_key_pressed(int ch)
{
	int r=0;
	r=on_key_pressed_mainboard(ch);

	refresh();

	if(r<0){
		endwin();
		exit(0);
	}
	
	return 0;
}


int on_key_pressed_mainboard(int ch)
{
	if(ch =='q'){
		return -1;
	}

	switch(ch){
	case KEYBOARD_REFRESH:		// ^L
		refresh_screen();
		break;
	case 'k':
	case KEYBOARD_UP:
		move_backward_1_line();
		break;
	case 'j':
	case KEYBOARD_DOWN:
		move_forward_1_line();
		break;
	case 'h':
	case KEYBOARD_LEFT:
		scroll_left_1_column();
		refresh_screen();
		break;
	case 'l':
	case KEYBOARD_RIGHT:
		scroll_right_1_column();
		refresh_screen();
		break;
	default:
		break;
	}
	display_status();

	return 0;
}
