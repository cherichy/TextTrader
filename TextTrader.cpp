// TextTrader
// writen by krenx@openctp
//
//
#include "TextTrader.h"

#ifdef _WIN32
#undef getch
#undef ungetch
#include <conio.h>
#include <windows.h>
#endif

#include <cstring>
#include <climits>
#include <cfloat>
#include <cmath>

#include <map>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <atomic>

#include "curses.h"
#include "INIReader.h"

#ifndef _WIN32
#define strnicmp strncasecmp
#endif

// 控制键定义
#define KEYBOARD_F(n) n
#define KEYBOARD_CTRL_F 13 // 6
#define KEYBOARD_CTRL_B 14 // 2
#define KEYBOARD_CTRL_U 15 // 21
#define KEYBOARD_CTRL_D 16 // 4
#define KEYBOARD_CTRL_E 17 // 5
#define KEYBOARD_CTRL_Y 18 // 25
#define KEYBOARD_UP 19 // 72
#define KEYBOARD_DOWN 20 // 80
#define KEYBOARD_LEFT 21 //75
#define KEYBOARD_RIGHT 22 // 77
#define KEYBOARD_PAGEUP 23 // 73
#define KEYBOARD_PAGEDOWN 24 // 81
#define KEYBOARD_ENTER 25 // 13
#define KEYBOARD_ESC 26 // 27
#define KEYBOARD_DELETE 27 // 8
#define KEYBOARD_REFRESH 28 // 12 ^L
#define KEYBOARD_NEXT 29 // 14 ^N
#define KEYBOARD_PREVIOUS 30 // 16 ^P

typedef struct {
	int error_no;
	char error_message[128];
} apierror_t;

apierror_t apierrorarray[]={
	{-1,"未知错误"},
	{-2,"API未启动"},
	{-3,"未连接"},
	{-4,"未登录"},
	{-5,"已登录"},
	{-6,"交易超时"}
};

std::string GBKToUtf8(const std::string& str)
{

#if defined(__WIN32) || defined(_MSC_VER) || defined(WIN64)
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
    wchar_t* wstr = new wchar_t[len + 1ull];
    memset(wstr, 0, len + 1ull);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wstr, len);
    len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char* cstr = new char[len + 1ull];
    memset(cstr, 0, len + 1ull);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, cstr, len, NULL, NULL);
    std::string res(cstr);

    delete[] wstr;
    delete[] cstr;

    return res;
#elif defined(__linux__) || defined(__GNUC__)
    // size_t len = str.size() * 2 + 1;
    // char* temp = new char[len];
    // if (EncodingConvert("gb2312", "utf-8", const_cast<char*>(str.c_str()), str.size(), temp, len)
    //     > = 0)
    // {

    //     std::string res;
    //     res.append(temp);
    //     delete[] temp;
    //     return res;
    // }
    // else
    // {

    //     delete[]temp;
    //     return str;
    // }
	return str;
#else
    std::cerr << "Unhandled operating system." << std::endl;
    return str;
#endif
}

std::string STR(const std::string& str){
	#ifdef PDC_FORCE_UTF8
	return GBKToUtf8(str);
	#else
	return str;
	#endif
}

class semaphore
{
public:
	explicit semaphore(int value = 1) :count(value) {}

	void wait()
	{
		std::unique_lock<std::mutex> lck(mt);
		if (--count < 0)
			cv.wait(lck);
	}

	void signal()
	{
		std::unique_lock<std::mutex> lck(mt);
		if (++count <= 0)
			cv.notify_one();
	}

private:
	int count;
	std::mutex mt;
	std::condition_variable cv;
};
void post_task(const std::function<void()>& task);

int apierrorcount=sizeof(apierrorarray)/sizeof(apierror_t);
char apierror_none[100]="";
char tradedate[20];
char tradetime[20];
char status_message[100];

char input_buffer[100];
std::vector<CThostFtdcInstrumentField> vInstruments;
std::map<std::string,size_t> mInstrumentIndex;
std::vector<quotation_t> vquotes;
std::vector<CThostFtdcOrderField> vOrders;
std::vector<CThostFtdcTradeField> vFilledOrders;
std::vector<stPosition_t> vPositions;
std::map<std::string, size_t> mPositionIndex;
std::vector<stAccount_t> vAccounts;
std::vector<CThostFtdcInputOrderField> vInputingOrders;
std::vector<CThostFtdcInputOrderActionField> vCancelingOrders;

CTradeRsp* pTradeRsp;
CMarketRsp* pMarketRsp;

int TradeConnectionStatus=CONNECTION_STATUS_DISCONNECTED;
int MarketConnectionStatus=CONNECTION_STATUS_DISCONNECTED;
std::mutex lock;
semaphore sem;
std::vector< std::function<void()> > vTasks;
std::atomic<int> seconds_delayed;

std::vector<CThostFtdcInvestorPositionField> vInvestorPositions;

enum WIN_TYPE {
	WIN_MAINBOARD =	0,
	WIN_ORDER =	1,
	WIN_FAVORITE =	2,
	WIN_CHART =	3,
	WIN_POSITION =	4,
	WIN_MONEY =	5,
	WIN_ORDERLIST =	6,
	WIN_FILLLIST =	7,
	WIN_MESSAGE =	8,
	WIN_SYSTEM =	9,
	WIN_COLUMN_SETTINGS	=100,
	WIN_SYMBOL =101
};

WIN_TYPE working_window = WIN_MAINBOARD;

typedef struct {
	char name[30];
	int width;
} column_item_t;

enum COL_ITEM {
	COL_SYMBOL			=	0,		// 合约
	COL_SYMBOL_NAME		=	1,		// 名称
	COL_CLOSE			=	2,		// 现价
	COL_PERCENT			=	3,		// 涨幅
	COL_VOLUME			=	4,		// 总手
	COL_TRADE_VOLUME	=	5,		// 现手
	COL_ADVANCE			=	6,		// 涨跌
	COL_OPEN			=	7,		// 开盘
	COL_HIGH			=	8,		// 最高
	COL_LOW				=	9,		// 最低
	COL_BID_PRICE		=	10,		// 买价
	COL_BID_VOLUME		=	11,		// 买量
	COL_ASK_PRICE		=	12,		// 卖价
	COL_ASK_VOLUME		=	13,		// 卖量
	COL_PREV_SETTLEMENT	=	14,		// 昨结
	COL_SETTLEMENT		=	15,		// 今结
	COL_PREV_CLOSE		=	16,		// 昨收
	COL_OPENINT			=	17,		// 今仓
	COL_PREV_OPENINT	=	18,		// 昨仓
	COL_AVERAGE_PRICE	=	19,		// 均价
	COL_HIGH_LIMIT		=	20,		// 涨停价
	COL_LOW_LIMIT		=	21,		// 跌停价
	COL_DATE			=	22,		// 日期
	COL_TIME			=	23,		// 时间
	COL_TRADE_DAY		=	24,		// 交易日
	COL_EXCHANGE		=	25,		// 交易所
};

column_item_t column_items[]={
	{"合约",		14},
	{"名称",		16},
	{"现价",		10},
	{"涨幅",		10},
	{"总手",		10},
	{"现手",		10},
	{"涨跌",		10},
	{"开盘",		10},
	{"最高",		10},
	{"最低",		10},
	{"买价",		10},
	{"买量",		10},
	{"卖价",		10},
	{"卖量",		10},
	{"昨结",		10},
	{"今结",		10},
	{"昨收",		10},
	{"今仓",		10},
	{"昨仓",		10},
	{"均价",		10},
	{"涨停",		10},
	{"跌停",		10},
	{"日期",		10},
	{"时间",		10},
	{"交易日",		10},
	{"交易所",		10}
};
std::vector<COL_ITEM> vcolumns;	// columns in order
std::map<COL_ITEM,bool> mcolumns;	// column select status

enum ORDER_ITEM{
	ORDERLIST_COL_SYMBOL		    = 0,		// 合约
	ORDERLIST_COL_SYMBOL_NAME	    = 1,		// 名称
	ORDERLIST_COL_DIRECTION		    = 2,		// 买卖
	ORDERLIST_COL_VOLUME		    = 3,		// 数量
	ORDERLIST_COL_VOLUME_FILLED	    = 4,		// 成交数量
	ORDERLIST_COL_PRICE			    = 5,		// 报价
	ORDERLIST_COL_AVG_PRICE		    = 6,		// 成交均价
	ORDERLIST_COL_APPLY_TIME	    = 7,		// 委托时间
	ORDERLIST_COL_UPDATE_TIME	    = 8,		// 更新时间
	ORDERLIST_COL_STATUS		    = 9,		// 报单状态
	ORDERLIST_COL_SH_FLAG			= 10,		// 投保
	ORDERLIST_COL_ORDERID			= 11,		// 报单号
	ORDERLIST_COL_EXCHANGE_NAME		= 12,		// 交易所名称
	ORDERLIST_COL_DESC				= 13,		// 备注
	ORDERLIST_COL_ACC_ID			= 14		// 账号
};

column_item_t orderlist_column_items[]={
	{"合约",		14},
	{"名称",		16},
	{"买卖",		6},
	{"数量",		5},
	{"成交",		5},
	{"报价",		10},
	{"成交均价",	10},
	{"委托时间",	10},
	{"更新时间",	10},
	{"报单状态",	8},
	{"投保",		4},
	{"报单号",		8},
	{"交易所",		10},
	{"备注",		30},
	{"账号",		10}
};
std::vector<ORDER_ITEM> vorderlist_columns;	// order list columns in order
std::map<ORDER_ITEM,bool> morderlist_columns;	// order list column select status

enum FILL_ITEM{
	FILLLIST_COL_SYMBOL			=	0,		// 合约
	FILLLIST_COL_SYMBOL_NAME	=	1,		// 名称
	FILLLIST_COL_DIRECTION		=	2,		// 买卖
	FILLLIST_COL_VOLUME			=	3,		// 数量
	FILLLIST_COL_PRICE			=	4,		// 价格
	FILLLIST_COL_TIME			=	5,		// 时间
	FILLLIST_COL_SH_FLAG		=	6,		// 投保
	FILLLIST_COL_ORDERID		=	7,		// 报单号
	FILLLIST_COL_FILLID			=	8,		// 成交号
	FILLLIST_COL_EXCHANGE_NAME	=	9,		// 交易所名称
	FILLLIST_COL_ACC_ID			=   10		// 账号
};

column_item_t filllist_column_items[]={
	{"合约",		10},
	{"名称",		16},
	{"买卖",		6},
	{"数量",		5},
	{"价格",		10},
	{"时间",		10},
	{"投保",		4},
	{"报单号",		8},
	{"成交号",		8},
	{"交易所",		10},
	{"账号",		10}
};
std::vector<FILL_ITEM> vfilllist_columns;	// fill list columns in order
std::map<FILL_ITEM,bool> mfilllist_columns;	// fill list column select status


enum POSITION_ITEM{
	POSITIONLIST_COL_SYMBOL				= 0,		// 合约
	POSITIONLIST_COL_SYMBOL_NAME		= 1,		// 名称
	POSITIONLIST_COL_VOLUME				= 2,		// 数量（冻结）
	POSITIONLIST_COL_AVG_PRICE			= 3,		// 均价
	POSITIONLIST_COL_PROFITLOSS			= 4,		// 盈亏（金额(点数/百分比)）
	POSITIONLIST_COL_MARGIN				= 5,		// 占用保证金
	POSITIONLIST_COL_AMOUNT				= 6,		// 持仓金额
	POSITIONLIST_COL_BUY_VOLUME			= 7,		// 买量（冻结）
	POSITIONLIST_COL_BUY_PRICE			= 8,		// 买均价
	POSITIONLIST_COL_BUY_PROFITLOSS		= 9,		// 买盈亏
	POSITIONLIST_COL_BUY_TODAY			= 10,		// 今买
	POSITIONLIST_COL_SELL_VOLUME		= 11,		// 卖量（冻结）
	POSITIONLIST_COL_SELL_PRICE			= 12,		// 卖均价
	POSITIONLIST_COL_SELL_PROFITLOSS	= 13,		// 卖盈亏
	POSITIONLIST_COL_SELL_TODAY			= 14,		// 今卖
	POSITIONLIST_COL_EXCHANGE_NAME		= 15,		// 交易所名称
	POSITIONLIST_COL_ACC_ID				= 16		// 账号
};
column_item_t positionlist_column_items[]={
	{"合约",		10},
	{"名称",		10},
	{"数量",		10},
	{"均价",		10},
	{"盈亏",		10},
	{"占用保证金",	10},
	{"持仓金额",	10},
	{"买量",		10},
	{"买均价",		10},
	{"买盈亏",		10},
	{"今买",		10},
	{"卖量",		10},
	{"卖均价",		10},
	{"卖盈亏",		10},
	{"今卖",		10},
	{"交易所",		10},
	{"账号",		10}
};
std::vector<POSITION_ITEM> vpositionlist_columns;	// position list columns in order
std::map<POSITION_ITEM,bool> mpositionlist_columns;	// position list column select status


enum ACCOUNT_ITEM{
	ACCLIST_COL_ACC_ID				= 0,		// 账号
	ACCLIST_COL_ACC_NAME			= 1,		// 名称
	ACCLIST_COL_PRE_BALANCE			= 2,		// 上日结存
	ACCLIST_COL_MONEY_IN			= 3,		// 入金
	ACCLIST_COL_MONEY_OUT			= 4,		// 出金
	ACCLIST_COL_FROZEN_MARGIN		= 5,		// 冻结保证金
	ACCLIST_COL_MONEY_FROZEN		= 6,		// 冻结资金
	ACCLIST_COL_FEE_FROZEN			= 7,		// 冻结手续费
	ACCLIST_COL_MARGIN				= 8,		// 占用保证金
	ACCLIST_COL_FEE					= 9,		// 手续费
	ACCLIST_COL_CLOSE_PROFIT_LOSS	= 10,		// 平仓盈亏
	ACCLIST_COL_FLOAT_PROFIT_LOSS	= 11,		// 持仓盈亏
	ACCLIST_COL_BALANCE_AVAILABLE	= 12,		// 可用资金
	ACCLIST_COL_BROKER_ID			= 13		// 经纪代码
};
column_item_t acclist_column_items[]={
	{"账号",		10},
	{"名称",		10},
	{"上日结存",	10},
	{"入金",		10},
	{"出金",		10},
	{"冻结保证金",	10},
	{"冻结资金",	10},
	{"冻结手续费",	10},
	{"占用保证金",	10},
	{"手续费",		10},
	{"平仓盈亏",	10},
	{"持仓盈亏",	10},
	{"可用资金",	10},
	{"经纪代码",	10}
};
std::vector<ACCOUNT_ITEM> vacclist_columns;	// position list columns in order
std::map<ACCOUNT_ITEM,bool> macclist_columns;	// position list column select status

// Mainboard Curses
int curr_line=0,curr_col=1,max_lines,max_cols=7;
int curr_pos=0,curr_col_pos=2;

// Order Curses
int order_curr_line=0,order_curr_col=0,order_max_lines,order_max_cols=9;
int order_curr_pos=0,order_curr_pos_ask=-1,order_curr_pos_bid=-1;
int order_symbol_index=-1;
double order_page_top_price=DBL_MAX,order_curr_price= DBL_MAX;
char order_curr_product_id[30];
char order_curr_accname[100];
char order_last_symbol[30];
double order_moving_at_price= DBL_MAX;
int order_is_moving=0;

// Column Settings Curses
int column_settings_curr_line=0,column_settings_curr_col=1,column_settings_max_lines;
int column_settings_curr_pos=0;

// Instrument Curses
int symbol_curr_line=1,symbol_curr_col=1,symbol_max_lines;
int symbol_curr_pos=0;
char symbol_curr_product_id[30];

// Order List Curses
int orderlist_curr_line=0,orderlist_curr_col=1,orderlist_max_lines,orderlist_max_cols=7;
int orderlist_curr_pos=0,orderlist_curr_col_pos=2;

// Filled Order List Curses
int filllist_curr_line=0,filllist_curr_col=1,filllist_max_lines,filllist_max_cols=7;
int filllist_curr_pos=0,filllist_curr_col_pos=2;

// Position List Curses
int positionlist_curr_line=0,positionlist_curr_col=1,positionlist_max_lines,positionlist_max_cols=7;
int positionlist_curr_pos=0,positionlist_curr_col_pos=2;

// Account List Curses
int acclist_curr_line=0,acclist_curr_col=1,acclist_max_lines,acclist_max_cols=7;
int acclist_curr_pos=0,acclist_curr_col_pos=2;

// Corner Curses
WINDOW *corner_win=NULL;
int corner_curr_line=0,corner_curr_col=1,corner_max_lines=5,corner_max_cols=20;
int corner_curr_pos=0,corner_curr_col_pos=0;
char corner_input[100],strsearching[30],strmatch[30];

// Order Corner Curses
WINDOW *order_corner_win=NULL;
int order_corner_curr_line=0,order_corner_curr_col=1,order_corner_max_lines=5,order_corner_max_cols=20;
int order_corner_curr_pos=0,order_corner_curr_col_pos=0;
char order_corner_input[100],order_strsearching[30],order_strmatch[30];

void status_print(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(status_message, sizeof(status_message), fmt, args);
	va_end(args);

	seconds_delayed = 0; // reset
	display_status();
	order_display_status();
	positionlist_display_status();
	acclist_display_status();
	orderlist_display_status();
	filllist_display_status();
	column_settings_display_status();
	symbol_display_status();
}

int main(int argc,char *argv[])
{
	std::string market_serv_addr, trade_serv_addr, broker, UserProductInfo, AuthCode, AppID, user, password, market_user, market_password;
	std::string trade_name_server,market_name_server;
	std::string ClientIPAddress,MacAddress,LoginRemark;

	INIReader reader("TextTrader.ini");

	// read config from file
	if (reader.ParseError() < 0) {
		std::cout << "open ini file failed." << std::endl;
		return -1;
	}
	market_serv_addr = reader.Get("market", "address", "");
	trade_serv_addr = reader.Get("trade", "address", "");
	broker = reader.Get("trade", "broker", "");
	UserProductInfo = reader.Get("trade", "UserProductInfo", "");
	AuthCode = reader.Get("trade", "AuthCode", "");
	AppID = reader.Get("trade", "AppID", "");
	user = reader.Get("trade", "user", "");
	password = reader.Get("trade", "password", "");
	ClientIPAddress = reader.Get("trade", "ClientIPAddress", "");
	MacAddress = reader.Get("trade", "MacAddress", "");
	LoginRemark = reader.Get("trade", "LoginRemark", "");
	market_user = reader.Get("market", "user", "");
	market_password = reader.Get("market", "password", "");
	trade_name_server = reader.Get("trade", "NameServer", "");
	market_name_server = reader.Get("market", "NameServer", "");

	int ch;
	char user_trade_flow_path[256],user_market_flow_path[256];

	// get user/password from terminal
	if (user.empty()) {
		std::cout << "UserID:";
		std::cin >> user;
		std::cout << "Password:";
		std::cin >> password;
	}

	// If no special market password then use trade's.
	if (market_user.empty())
		market_user = user;
	if (market_password.empty())
		market_password = password;

	// Market	
	pMarketRsp=new CMarketRsp();
	strcpy(pMarketRsp->marketserv, market_serv_addr.c_str());
	sprintf(user_market_flow_path,"market");
	strcpy(pMarketRsp->broker, broker.c_str());
	strcpy(pMarketRsp->user, market_user.c_str());
	strcpy(pMarketRsp->passwd, market_password.c_str());
	pMarketRsp->m_pMarketReq=CThostFtdcMdApi::CreateFtdcMdApi(user_market_flow_path);
	pMarketRsp->m_pMarketReq->RegisterSpi(pMarketRsp);
	if(!market_name_server.empty()){
		CThostFtdcFensUserInfoField FensUserInfo{};
		memset(&FensUserInfo,0x00,sizeof(FensUserInfo));
		strncpy(FensUserInfo.BrokerID,broker.c_str(),sizeof(FensUserInfo.BrokerID)-1);
		strncpy(FensUserInfo.UserID,broker.c_str(),sizeof(FensUserInfo.UserID)-1);
		FensUserInfo.LoginMode = THOST_FTDC_LM_Trade;
		pMarketRsp->m_pMarketReq->RegisterFensUserInfo(&FensUserInfo);
		pMarketRsp->m_pMarketReq->RegisterNameServer((char*)market_name_server.c_str());
	}else{
		pMarketRsp->m_pMarketReq->RegisterFront((char*)market_serv_addr.c_str());
	}
	pMarketRsp->m_pMarketReq->Init();


	// Trade
	stAccount_t Account;
	memset(&Account,0x00,sizeof(Account));
	strcpy(Account.AccName, user.c_str());
	strcpy(Account.BrokerID, broker.c_str());
	strcpy(Account.AccID, user.c_str());
	vAccounts.push_back(Account);

	pTradeRsp=new CTradeRsp();
	strcpy(pTradeRsp->broker, broker.c_str());
	strcpy(pTradeRsp->user, user.c_str());
	strcpy(pTradeRsp->passwd, password.c_str());
	strcpy(pTradeRsp->UserProductInfo,UserProductInfo.c_str());
	strcpy(pTradeRsp->ClientIPAddress,ClientIPAddress.c_str());
	strcpy(pTradeRsp->MacAddress,MacAddress.c_str());
	strcpy(pTradeRsp->LoginRemark,LoginRemark.c_str());
	strcpy(pTradeRsp->AppID,AppID.c_str());
	strcpy(pTradeRsp->AuthCode,AuthCode.c_str());
	strcpy(pTradeRsp->name, user.c_str());
	strcpy(pTradeRsp->tradeserv, trade_serv_addr.c_str());
	strcpy(order_curr_accname, pTradeRsp->name);
	sprintf(user_trade_flow_path,"%s_%s_trade", broker.c_str(), user.c_str());
	pTradeRsp->m_pTradeReq=CThostFtdcTraderApi::CreateFtdcTraderApi(user_trade_flow_path);
	pTradeRsp->m_pTradeReq->RegisterSpi(pTradeRsp);
	if(!trade_name_server.empty()){
		CThostFtdcFensUserInfoField FensUserInfo{};
		memset(&FensUserInfo,0x00,sizeof(FensUserInfo));
		strncpy(FensUserInfo.BrokerID,broker.c_str(),sizeof(FensUserInfo.BrokerID)-1);
		strncpy(FensUserInfo.UserID,user.c_str(),sizeof(FensUserInfo.UserID)-1);
		FensUserInfo.LoginMode = THOST_FTDC_LM_Trade;
		pTradeRsp->m_pTradeReq->RegisterFensUserInfo(&FensUserInfo);
		pTradeRsp->m_pTradeReq->RegisterNameServer((char*)trade_name_server.c_str());
	}else{
		pTradeRsp->m_pTradeReq->RegisterFront((char*)trade_serv_addr.c_str());
	}
	pTradeRsp->m_pTradeReq->SubscribePrivateTopic(THOST_TERT_RESTART);
	pTradeRsp->m_pTradeReq->SubscribePublicTopic(THOST_TERT_RESTART);
	pTradeRsp->m_pTradeReq->Init();


	// Init Columns
	vcolumns = {
        COL_SYMBOL,
        COL_SYMBOL_NAME,
        COL_CLOSE,
        COL_PERCENT,
        COL_VOLUME,
        COL_TRADE_VOLUME,
        COL_BID_PRICE,
        COL_BID_VOLUME,
        COL_ASK_PRICE,
        COL_ASK_VOLUME,
        COL_HIGH_LIMIT,
        COL_LOW_LIMIT,
        COL_PREV_SETTLEMENT,
        COL_ADVANCE,
        COL_OPEN,
        COL_HIGH,
        COL_LOW,
        COL_AVERAGE_PRICE,
        COL_PREV_CLOSE,
        COL_OPENINT,
        COL_PREV_OPENINT,
        COL_SETTLEMENT,
        COL_DATE,
        COL_TIME,
        COL_EXCHANGE,
        COL_TRADE_DAY
    };
	for(auto col: vcolumns){
		mcolumns[col] = true;
	}


	// Init Order List Columns
	vorderlist_columns={
		ORDERLIST_COL_ACC_ID,
		ORDERLIST_COL_SYMBOL,
		ORDERLIST_COL_SYMBOL_NAME,
		ORDERLIST_COL_DIRECTION,
		ORDERLIST_COL_VOLUME,
		ORDERLIST_COL_VOLUME_FILLED,
		ORDERLIST_COL_PRICE,
		ORDERLIST_COL_AVG_PRICE,
		ORDERLIST_COL_APPLY_TIME,
		ORDERLIST_COL_UPDATE_TIME,
		ORDERLIST_COL_STATUS,
		ORDERLIST_COL_SH_FLAG,
		ORDERLIST_COL_ORDERID,
		ORDERLIST_COL_EXCHANGE_NAME,
		ORDERLIST_COL_DESC
	};
	for(auto col:vorderlist_columns){
		morderlist_columns[col] = true;
	}
	
	// Init Fill List Columns
	vfilllist_columns={
		FILLLIST_COL_ACC_ID,
		FILLLIST_COL_SYMBOL,
		FILLLIST_COL_SYMBOL_NAME,
		FILLLIST_COL_DIRECTION,
		FILLLIST_COL_VOLUME,
		FILLLIST_COL_PRICE,
		FILLLIST_COL_TIME,
		FILLLIST_COL_SH_FLAG,
		FILLLIST_COL_ORDERID,
		FILLLIST_COL_FILLID,
		FILLLIST_COL_EXCHANGE_NAME
	};
	for(auto col: vfilllist_columns){
		mfilllist_columns[col] = true;
	}

	// Init Position List Columns
	vpositionlist_columns = {
		POSITIONLIST_COL_ACC_ID,
		POSITIONLIST_COL_SYMBOL,
		POSITIONLIST_COL_SYMBOL_NAME,
		POSITIONLIST_COL_VOLUME,
		POSITIONLIST_COL_AVG_PRICE,
		POSITIONLIST_COL_PROFITLOSS,
		POSITIONLIST_COL_MARGIN,
		POSITIONLIST_COL_AMOUNT,
		POSITIONLIST_COL_BUY_VOLUME,
		POSITIONLIST_COL_BUY_PRICE,
		POSITIONLIST_COL_BUY_PROFITLOSS,
		POSITIONLIST_COL_BUY_TODAY,
		POSITIONLIST_COL_SELL_VOLUME,
		POSITIONLIST_COL_SELL_PRICE,
		POSITIONLIST_COL_SELL_PROFITLOSS,
		POSITIONLIST_COL_SELL_TODAY,
		POSITIONLIST_COL_EXCHANGE_NAME
	};
	for(auto col:vpositionlist_columns){
		mpositionlist_columns[col]=true;
	}

	// Init Account List Columns
	vacclist_columns = {
		ACCLIST_COL_ACC_ID,
		ACCLIST_COL_ACC_NAME,
		ACCLIST_COL_PRE_BALANCE,
		ACCLIST_COL_MONEY_IN,
		ACCLIST_COL_MONEY_OUT,
		ACCLIST_COL_FROZEN_MARGIN,
		ACCLIST_COL_MONEY_FROZEN,
		ACCLIST_COL_FEE_FROZEN,
		ACCLIST_COL_MARGIN,
		ACCLIST_COL_FEE,
		ACCLIST_COL_CLOSE_PROFIT_LOSS,
		ACCLIST_COL_FLOAT_PROFIT_LOSS,
		ACCLIST_COL_BALANCE_AVAILABLE,
		ACCLIST_COL_BROKER_ID
	};
	for(auto col:vacclist_columns){
		macclist_columns[col] = true;
	}

	std::thread workthread(&work_thread);
	std::thread timerthread(&time_thread);

	// Idle
	while(true){
#ifdef _WIN32
		ch=_getch();
#else
		ch=getchar();
#endif
		if (ch == 224) {
#ifdef _WIN32
			ch = _getch();
#else
			ch = getchar();
#endif
			switch (ch)
			{
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
			case 81: // 下一页
				ch = KEYBOARD_PAGEDOWN;
				break;
			case 73: // 上一页
				ch = KEYBOARD_PAGEUP;
				break;
			case 6: // ^F（下一页）
				ch = KEYBOARD_CTRL_F;
				break;
			case 2: // ^B（上一页）
				ch = KEYBOARD_CTRL_B;
				break;
			case 21: // ^U（上半页）
				ch = KEYBOARD_CTRL_U;
				break;
			case 4: // ^D（下半页）
				ch = KEYBOARD_CTRL_D;
				break;
			case 5: // ^E（向前滚动）
				ch = KEYBOARD_CTRL_E;
				break;
			case 25: // ^Y（向后滚动）
				ch = KEYBOARD_CTRL_Y;
				break;
			case 12: // ^L（刷新）
				ch = KEYBOARD_REFRESH;
				break;
			case 14: // ^N（下一行）
				ch = KEYBOARD_NEXT;
				break;
			case 16: // ^P（上一行）
				ch = KEYBOARD_PREVIOUS;
				break;
			default:
				break;
			}
		}else if (ch == 0) {
#ifdef _WIN32
			ch = _getch();
#else
			ch = getchar();
#endif
			switch (ch)
			{
			case 59: // F1
			case 60: // F2
			case 61: // F3
			case 62: // F4
			case 63: // F5
			case 64: // F6
			case 65: // F7
			case 66: // F8
			case 67: // F9
			case 68: // F10
				ch = KEYBOARD_F(ch - 59 + 1);
				break;
			default:
				break;
			}
		}
		else {
			switch (ch)
			{
			case 13: // ENTER（回车）
				ch = KEYBOARD_ENTER;
				break;
			case 27: // ESC（取消）
				ch = KEYBOARD_ESC;
				break;
			case 8: // DELETE（删除）
				ch = KEYBOARD_DELETE;
				break;
			case 6: // ^F（下一页）
				ch = KEYBOARD_CTRL_F;
				break;
			case 2: // ^B（上一页）
				ch = KEYBOARD_CTRL_B;
				break;
			case 21: // ^U（上半页）
				ch = KEYBOARD_CTRL_U;
				break;
			case 4: // ^D（下半页）
				ch = KEYBOARD_CTRL_D;
				break;
			case 5: // ^E（向前滚动）
				ch = KEYBOARD_CTRL_E;
				break;
			case 25: // ^Y（向后滚动）
				ch = KEYBOARD_CTRL_Y;
				break;
			case 12: // ^L（刷新）
				ch = KEYBOARD_REFRESH;
				break;
			case 14: // ^N（下一行）
				ch = KEYBOARD_NEXT;
				break;
			case 16: // ^P（上一行）
				ch = KEYBOARD_PREVIOUS;
				break;
			default:
				break;
			}
		}
		post_task(std::bind(on_key_pressed, ch));
		//if(ch=='q'){
		//	break;
		//}
	}

	return 0;
}

void post_task(const std::function<void()>& task)
{
	lock.lock();
	vTasks.push_back(task);
	lock.unlock();
	sem.signal();
}

double GetProfitLoss(const char* InstrumentID)
{
	return GetBuyProfitLoss(InstrumentID) + GetSellProfitLoss(InstrumentID);
}

double GetBuyProfitLoss(const char* InstrumentID)
{
	auto iterInstrument = mInstrumentIndex.find(InstrumentID);
	if (iterInstrument == mInstrumentIndex.end())
		return 0;
	size_t i = iterInstrument->second;

	int precision = vquotes[i].precision;
	double high_limit = vquotes[i].DepthMarketData.UpperLimitPrice;
	double low_limit = vquotes[i].DepthMarketData.LowerLimitPrice;
	double PriceTick = vquotes[i].Instrument.PriceTick;
	double close_price = vquotes[i].DepthMarketData.LastPrice;
	double prev_settle = vquotes[i].DepthMarketData.PreSettlementPrice;
	int quantity = vquotes[i].DepthMarketData.Volume;
	int nPosi = 0, nBuyPosi = 0, nSellPosi = 0;
	double AvgBuyPrice = 0, AvgSellPrice = 0;

	double offset;
	double ratio;
	if (close_price == DBL_MAX || close_price == 0 || prev_settle == DBL_MAX || prev_settle == 0) {
		offset = 0;
		ratio = 0;
	}
	else {
		offset = close_price - prev_settle;
		ratio = (close_price - prev_settle) / prev_settle * 100.0;
	}

	std::vector<stPosition_t>::iterator iter;
	for (iter = vPositions.begin(); iter != vPositions.end(); iter++) {
		if (strcmp(iter->AccID, order_curr_accname) == 0 && strcmp(iter->InstrumentID, vquotes[i].product_id) == 0)
			break;
	}
	if (iter != vPositions.end()) {
		nPosi = iter->Volume;
		nBuyPosi = iter->BuyVolume;
		nSellPosi = iter->SellVolume;
		AvgBuyPrice = iter->AvgBuyPrice;
		AvgSellPrice = iter->AvgSellPrice;
	}

	double PL = 0;
	if (nBuyPosi && close_price != DBL_MAX)
		PL = (close_price - AvgBuyPrice) * nBuyPosi * vquotes[i].Instrument.VolumeMultiple;

	return PL;
}

double GetSellProfitLoss(const char* InstrumentID)
{
	auto iterInstrument = mInstrumentIndex.find(InstrumentID);
	if (iterInstrument == mInstrumentIndex.end())
		return 0;
	size_t i = iterInstrument->second;

	int precision = vquotes[i].precision;
	double high_limit = vquotes[i].DepthMarketData.UpperLimitPrice;
	double low_limit = vquotes[i].DepthMarketData.LowerLimitPrice;
	double PriceTick = vquotes[i].Instrument.PriceTick;
	double close_price = vquotes[i].DepthMarketData.LastPrice;
	double prev_settle = vquotes[i].DepthMarketData.PreSettlementPrice;
	int quantity = vquotes[i].DepthMarketData.Volume;
	int nPosi = 0, nBuyPosi = 0, nSellPosi = 0;
	double AvgBuyPrice = 0, AvgSellPrice = 0;

	double offset;
	double ratio;
	if (close_price == DBL_MAX || close_price == 0 || prev_settle == DBL_MAX || prev_settle == 0) {
		offset = 0;
		ratio = 0;
	}
	else {
		offset = close_price - prev_settle;
		ratio = (close_price - prev_settle) / prev_settle * 100.0;
	}

	std::vector<stPosition_t>::iterator iter;
	for (iter = vPositions.begin(); iter != vPositions.end(); iter++) {
		if (strcmp(iter->AccID, order_curr_accname) == 0 && strcmp(iter->InstrumentID, vquotes[i].product_id) == 0)
			break;
	}
	if (iter != vPositions.end()) {
		nPosi = iter->Volume;
		nBuyPosi = iter->BuyVolume;
		nSellPosi = iter->SellVolume;
		AvgBuyPrice = iter->AvgBuyPrice;
		AvgSellPrice = iter->AvgSellPrice;
	}

	double PL = 0;
	if (nSellPosi && close_price != DBL_MAX)
		PL = (AvgSellPrice - close_price) * nSellPosi * vquotes[i].Instrument.VolumeMultiple;

	return PL;
}

void HandleStatusClear()
{
	memset(status_message, 0x00, sizeof(status_message));
	seconds_delayed = 0;
}

void HandleQueryAccount()
{
	if (TradeConnectionStatus != CONNECTION_STATUS_LOGINOK)
		return;

	CThostFtdcQryTradingAccountField Req{};

	memset(&Req, 0x00, sizeof(Req));
	strcpy(Req.BrokerID, pTradeRsp->broker);
	strcpy(Req.InvestorID, pTradeRsp->user);
	pTradeRsp->m_pTradeReq->ReqQryTradingAccount(&Req, 0);
}

void HandleTickTimeout()
{
	switch(working_window){
	case WIN_MAINBOARD:
		display_status();
		break;
	case WIN_ORDER:
		order_display_status();
		break;
	case WIN_COLUMN_SETTINGS:
		column_settings_display_status();
		break;
	case WIN_SYMBOL:
		symbol_display_status();
		break;
	case WIN_ORDERLIST:
		orderlist_display_status();
		break;
	case WIN_FILLLIST:
		filllist_display_status();
		break;
	case WIN_POSITION:
		positionlist_display_status();
		break;
	case WIN_MONEY:
		acclist_display_status();
		break;
	case WIN_MESSAGE:
		break;
	default:
		break;
	}
	refresh();
	if(corner_win){
		corner_redraw();
		wrefresh(corner_win);
	}
	if(order_corner_win){
		order_corner_redraw();
		wrefresh(order_corner_win);
	}
}

int goto_symbol_window_from_mainboard()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	strcpy(symbol_curr_product_id,vquotes[curr_pos+curr_line-1].product_id);
	working_window=WIN_SYMBOL;
	symbol_refresh_screen();
	unsubscribe(UINT_MAX);
	
	return 0;
}
int goto_order_window_from_mainboard()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	order_symbol_index=curr_pos+curr_line-1;
	working_window=WIN_ORDER;
	order_curr_price=0;
	order_page_top_price=0;
	order_refresh_screen();
	order_centralize_current_price();
	unsubscribe(UINT_MAX);
	subscribe(order_symbol_index);

	return 0;
}

int goto_orderlist_window_from_mainboard()
{
	working_window=WIN_ORDERLIST;
	orderlist_refresh_screen();
	unsubscribe(UINT_MAX);
	
	return 0;
}

int goto_filllist_window_from_mainboard()
{
	working_window=WIN_FILLLIST;
	filllist_refresh_screen();
	unsubscribe(UINT_MAX);
	
	return 0;
}

int goto_positionlist_window_from_mainboard()
{
	working_window=WIN_POSITION;
	positionlist_refresh_screen();
	unsubscribe(UINT_MAX);
	
	return 0;
}

int goto_acclist_window_from_mainboard()
{
	working_window=WIN_MONEY;
	acclist_refresh_screen();
	unsubscribe(UINT_MAX);
	
	return 0;
}

int goto_column_settings_window_from_mainboard()
{
	working_window=WIN_COLUMN_SETTINGS;
	column_settings_curr_line=1;
	column_settings_refresh_screen();
	unsubscribe(UINT_MAX);
	
	return 0;
}

int move_forward_1_page()
{
	int i;
	for(i=0;i<max_lines;i++)
		scroll_forward_1_line();
	return 0;
}
int move_backward_1_page()
{
	int i;
	for(i=0;i<max_lines;i++)
		scroll_backward_1_line();

	return 0;
}
int move_forward_half_page()
{
	int i;
	for(i=0;i<max_lines/2;i++)
		scroll_forward_1_line();
	return 0;
}
int move_backward_half_page()
{
	int i;
	for(i=0;i<max_lines/2;i++)
		scroll_backward_1_line();
	return 0;
}
int goto_page_top()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}
int goto_page_bottom()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(curr_line==vquotes.size()-curr_pos)	// Already bottom
		return 0;
	if(vquotes.size()-curr_pos<max_lines){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=vquotes.size()-curr_pos;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=max_lines;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}
int goto_page_middle()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(vquotes.size()-curr_pos==1)	// Only 1 line
		return 0;
	if(vquotes.size()-curr_pos<max_lines){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=(vquotes.size()-curr_pos)/2+1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=max_lines/2+1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}

int scroll_forward_1_line()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(curr_pos==vquotes.size()-1){	//Already bottom
		return 0;
	}
	move(1,0);
	setscrreg(1,max_lines);
	scroll(stdscr);
	setscrreg(0,max_lines+1);
	
	if(curr_line==1){
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		curr_line--;
	}
	
	unsubscribe(curr_pos);
	curr_pos++;
	if(vquotes.size()-curr_pos>=max_lines){
		display_quotation(curr_pos+max_lines-1);
		subscribe(curr_pos+max_lines-1);
	}
	display_title();

	return 0;
}
int scroll_backward_1_line()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(curr_pos==0){	//Already top
		return 0;
	}
	move(1,0);
	setscrreg(1,max_lines);
	scrl(-1);
	setscrreg(0,max_lines+1);
	
	if(curr_line==max_lines){
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		unsubscribe(curr_pos+max_lines-1);
	}else{
		curr_line++;
	}
	
	curr_pos--;
	display_quotation(curr_pos);
	subscribe(curr_pos);
	display_status();

	return 0;
}

int move_forward_1_line()
{
	if(vquotes.empty())
		return 0;
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
		unsubscribe(curr_pos);
		curr_pos++;
		display_quotation(curr_pos+max_lines-1);	// new line
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		subscribe(curr_pos+max_lines-1);
	}

	return 0;
}
int scroll_left_1_column()
{
	if(curr_col_pos==2)
		return 0;
	while(mcolumns[vcolumns[--curr_col_pos]]==false); //	取消所在列的反白显示
	display_title();
	if(vquotes.empty())
		return 0;
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
		display_quotation(i);
	
	return 0;
}
int scroll_right_1_column()
{
	if(curr_col_pos==sizeof(column_items)/sizeof(column_item_t)-1)
		return 0;
	while(mcolumns[vcolumns[++curr_col_pos]]==false); //	取消所在列的反白显示
	display_title();
	if(vquotes.empty())
		return 0;
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
		display_quotation(i);

	return 0;
}

int move_backward_1_line()
{
	if(vquotes.empty())
		return 0;
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
		unsubscribe(curr_pos+max_lines-1);
		curr_pos--;
		display_quotation(curr_pos);
		subscribe(curr_pos);
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}
int goto_file_top()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(curr_line==1 && curr_pos==0)	// Already top
		return 0;
	if(curr_pos==0){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		size_t n;
		n=vquotes.size()<max_lines?vquotes.size():max_lines;
		curr_pos=0;
		curr_line=1;
		unsubscribe(UINT_MAX);
		for(size_t i=0;i<n;i++){
			display_quotation(curr_pos+i);
			subscribe(curr_pos+i);
		}
// 		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	
	return 0;
}
int goto_file_bottom()
{
	if(vquotes.empty())
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(curr_line==vquotes.size()-curr_pos)	// Already bottom
		return 0;
	if(vquotes.size()-curr_pos<=max_lines){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=vquotes.size()-curr_pos;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_pos=vquotes.size()-max_lines;
		curr_line=max_lines;
		unsubscribe(UINT_MAX);
		for(int i=0;i<max_lines;i++){
			display_quotation(curr_pos+i);
			subscribe(curr_pos+i);
		}
// 		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	return 0;
}

void focus_quotation(int index)
{
	if(vquotes.empty())
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

CTradeRsp::CTradeRsp()
{
	memset(name, 0x00, sizeof(name));
	memset(user, 0x00, sizeof(user));
	memset(passwd, 0x00, sizeof(passwd));
	memset(broker, 0x00, sizeof(broker));
	memset(tradedate, 0x00, sizeof(tradedate));
	memset(tradetime, 0x00, sizeof(tradetime));
	memset(tradeserv, 0x00, sizeof(tradeserv));
	memset(license, 0x00, sizeof(license));
}
CTradeRsp::~CTradeRsp()
= default;

//已连接
void CTradeRsp::OnFrontConnected()
{
	post_task(std::bind(&CTradeRsp::HandleFrontConnected,this));
}
//未连接
void CTradeRsp::OnFrontDisconnected(int nReason)
{
	post_task(std::bind(&CTradeRsp::HandleFrontDisconnected,this,nReason));
}

///认证响应
void CTradeRsp::OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	post_task(std::bind(&CTradeRsp::HandleRspAuthenticate,this,*pRspAuthenticateField,*pRspInfo,nRequestID,bIsLast));
}

//登录应答
void CTradeRsp::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin,CThostFtdcRspInfoField *pRspInfo,int nRequestID,bool bIsLast)
{
	CThostFtdcRspUserLoginField RspUserLogin{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&RspUserLogin, 0x00, sizeof(RspUserLogin));
	memset(&RspInfo, 0x00, sizeof(RspInfo));
	if (pRspUserLogin)
		memcpy(&RspUserLogin, pRspUserLogin, sizeof(RspUserLogin));
	if (pRspInfo)
		memcpy(&RspInfo, pRspInfo, sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspUserLogin,this,RspUserLogin,RspInfo,nRequestID,bIsLast));
}

//登出应答
void CTradeRsp::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout,CThostFtdcRspInfoField *pRspInfo,int nRequestID,bool bIsLast)
{
	CThostFtdcUserLogoutField UserLogout{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&UserLogout, 0x00, sizeof(UserLogout));
	memset(&RspInfo, 0x00, sizeof(RspInfo));
	if (pUserLogout)
		memcpy(&UserLogout, pUserLogout, sizeof(UserLogout));
	if (pRspInfo)
		memcpy(&RspInfo, pRspInfo, sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspUserLogout,this, UserLogout,RspInfo,nRequestID,bIsLast));
}

//查询合约应答
void CTradeRsp::OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcInstrumentField Instrument{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&Instrument,0x00,sizeof(Instrument));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pInstrument)
		memcpy(&Instrument,pInstrument,sizeof(Instrument));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));

	post_task(std::bind(&CTradeRsp::HandleRspQryInstrument,this,Instrument,RspInfo,nRequestID,bIsLast));
}

void CTradeRsp::OnRspQryDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
}

void CTradeRsp::OnRspQryOrder(CThostFtdcOrderField *pOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcOrderField Order{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&Order,0x00,sizeof(Order));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pOrder)
		memcpy(&Order,pOrder,sizeof(Order));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspQryOrder,this,Order,RspInfo,nRequestID,bIsLast));
}

void CTradeRsp::OnRspQryTrade(CThostFtdcTradeField *pTrade, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcTradeField Trade{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&Trade,0x00,sizeof(Trade));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pTrade)
		memcpy(&Trade,pTrade,sizeof(Trade));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspQryTrade,this,Trade,RspInfo,nRequestID,bIsLast));
}

void CTradeRsp::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcInvestorPositionField InvestorPosition{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&InvestorPosition,0x00,sizeof(InvestorPosition));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pInvestorPosition)
		memcpy(&InvestorPosition,pInvestorPosition,sizeof(InvestorPosition));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspQryInvestorPosition,this,InvestorPosition,RspInfo,nRequestID,bIsLast));
}

	// 查询持仓明细
void CTradeRsp::OnRspQryInvestorPositionDetail(CThostFtdcInvestorPositionDetailField *pInvestorPositionDetail, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
}

///请求查询资金账户响应
void CTradeRsp::OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcTradingAccountField TradingAccount{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&TradingAccount,0x00,sizeof(TradingAccount));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pTradingAccount)
		memcpy(&TradingAccount,pTradingAccount,sizeof(TradingAccount));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspQryTradingAccount,this,TradingAccount,RspInfo,nRequestID,bIsLast));
}

///报单录入请求响应
void CTradeRsp::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcInputOrderField InputOrder{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&InputOrder,0x00,sizeof(InputOrder));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pInputOrder)
		memcpy(&InputOrder,pInputOrder,sizeof(InputOrder));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspOrderInsert,this,InputOrder,RspInfo,nRequestID,bIsLast));
}

///报单操作请求响应
void CTradeRsp::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	CThostFtdcInputOrderActionField InputOrderAction{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&InputOrderAction,0x00,sizeof(InputOrderAction));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pInputOrderAction)
		memcpy(&InputOrderAction,pInputOrderAction,sizeof(InputOrderAction));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleRspOrderAction,this,InputOrderAction,*pRspInfo,nRequestID,bIsLast));
}

void CTradeRsp::OnRtnOrder(CThostFtdcOrderField *pOrder)
{
	CThostFtdcOrderField Order{};

	memset(&Order,0x00,sizeof(Order));
	if(pOrder)
		memcpy(&Order,pOrder,sizeof(Order));
	post_task(std::bind(&CTradeRsp::HandleRtnOrder,this,Order));
}

void CTradeRsp::OnRtnTrade(CThostFtdcTradeField *pTrade)
{
	CThostFtdcTradeField Trade{};

	memset(&Trade,0x00,sizeof(Trade));
	if(pTrade)
		memcpy(&Trade,pTrade,sizeof(Trade));
	post_task(std::bind(&CTradeRsp::HandleRtnTrade,this,Trade));
}

void CTradeRsp::OnErrRtnOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo)
{
	CThostFtdcInputOrderField InputOrder{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&InputOrder,0x00,sizeof(InputOrder));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pInputOrder)
		memcpy(&InputOrder,pInputOrder,sizeof(InputOrder));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleErrRtnOrderInsert,this,InputOrder,RspInfo));
}

void CTradeRsp::OnErrRtnOrderAction(CThostFtdcOrderActionField *pOrderAction, CThostFtdcRspInfoField *pRspInfo)
{
	CThostFtdcOrderActionField OrderAction{};
	CThostFtdcRspInfoField RspInfo{};

	memset(&OrderAction,0x00,sizeof(OrderAction));
	memset(&RspInfo,0x00,sizeof(RspInfo));
	if(pOrderAction)
		memcpy(&OrderAction,pOrderAction,sizeof(OrderAction));
	if(pRspInfo)
		memcpy(&RspInfo,pRspInfo,sizeof(RspInfo));
	post_task(std::bind(&CTradeRsp::HandleErrRtnOrderAction,this,OrderAction,RspInfo));
}


//Quot
CMarketRsp::CMarketRsp()
{
	memset(name, 0x00, sizeof(name));
	memset(user, 0x00, sizeof(user));
	memset(passwd, 0x00, sizeof(passwd));
	memset(broker, 0x00, sizeof(broker));
	memset(tradedate, 0x00, sizeof(tradedate));
	memset(tradetime, 0x00, sizeof(tradetime));
	memset(tradeserv, 0x00, sizeof(tradeserv));
	memset(marketserv, 0x00, sizeof(marketserv));
	memset(license, 0x00, sizeof(license));

}
CMarketRsp::~CMarketRsp()
= default;
void CMarketRsp::OnFrontConnected()
{
	post_task(std::bind(&CMarketRsp::HandleFrontConnected,this));
}
void CMarketRsp::OnFrontDisconnected(int nReason)
{
	post_task(std::bind(&CMarketRsp::HandleFrontDisconnected,this,nReason));
}
void CMarketRsp::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin,CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	post_task(std::bind(&CMarketRsp::HandleRspUserLogin,this,*pRspUserLogin,*pRspInfo,nRequestID,bIsLast));
}
void CMarketRsp::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	post_task(std::bind(&CMarketRsp::HandleRspUserLogout,this,*pUserLogout,*pRspInfo,nRequestID,bIsLast));
}
void CMarketRsp::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{

}
void CMarketRsp::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *pSpecificInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
	
}
//行情服务的深度行情通知
void CMarketRsp::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData)
{
	post_task(std::bind(&CMarketRsp::HandleRtnDepthMarketData,this,*pDepthMarketData));
}

void display_quotation(size_t index)
{
	int i=index,y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_MAINBOARD)
		return;
	getmaxyx(stdscr,maxy,maxx);
	
	if(i<curr_pos || i>curr_pos+max_lines-1)
		return;
	y=i-curr_pos+1;
	x=0;

	move(y,0);
	clrtoeol();

	double previous_close = vquotes[i].DepthMarketData.PreClosePrice;
	if (previous_close == DBL_MAX || fabs(previous_close) < 0.000001)
		previous_close = vquotes[i].DepthMarketData.PreSettlementPrice;

	
	for(auto iter=vcolumns.begin();iter!=vcolumns.end();iter++,pos++){
		if(mcolumns[*iter]==false)
			continue;
		if(*iter!=COL_SYMBOL && *iter!=COL_SYMBOL_NAME && pos<curr_col_pos)
			continue;
		if(maxx-x<column_items[*iter].width)
			break;
		switch(*iter){
		case COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL].width,vquotes[i].product_id);
			x+=column_items[COL_SYMBOL].width;
			break;
		case COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL_NAME].width,STR(vquotes[i].product_name).c_str());
			x+=column_items[COL_SYMBOL_NAME].width+1;
			break;
		case COL_CLOSE:
			if(vquotes[i].DepthMarketData.LastPrice ==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_CLOSE].width,'-');
			else
			mvprintw(y,x,"%*.*f",column_items[COL_CLOSE].width,vquotes[i].precision,vquotes[i].DepthMarketData.LastPrice);
			x+=column_items[COL_CLOSE].width+1;
			break;
		case COL_PERCENT:
			if(previous_close ==DBL_MAX || fabs(previous_close) < 0.000001 || vquotes[i].DepthMarketData.LastPrice==DBL_MAX || fabs(vquotes[i].DepthMarketData.LastPrice) < 0.000001)
				mvprintw(y,x,"%*c",column_items[COL_PERCENT].width,'-');
			else
				mvprintw(y,x,"%*.1f%%",column_items[COL_PERCENT].width-1,(vquotes[i].DepthMarketData.LastPrice- previous_close)/ previous_close *100.0);
			x+=column_items[COL_PERCENT].width+1;
			break;
		case COL_ADVANCE:
			if(previous_close == DBL_MAX || fabs(previous_close) < 0.000001 || vquotes[i].DepthMarketData.LastPrice == DBL_MAX || fabs(vquotes[i].DepthMarketData.LastPrice) < 0.000001)
				mvprintw(y,x,"%*c",column_items[COL_ADVANCE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_ADVANCE].width-1,vquotes[i].precision,vquotes[i].DepthMarketData.LastPrice- previous_close);
			x+=column_items[COL_ADVANCE].width+1;
			break;
		case COL_VOLUME:
			mvprintw(y,x,"%*d",column_items[COL_VOLUME].width,vquotes[i].DepthMarketData.Volume);
			x+=column_items[COL_VOLUME].width+1;
			break;
		case COL_BID_PRICE:
			if(vquotes[i].DepthMarketData.BidPrice1==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_BID_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_BID_PRICE].width,vquotes[i].precision,vquotes[i].DepthMarketData.BidPrice1);
			x+=column_items[COL_BID_PRICE].width+1;
			break;
		case COL_BID_VOLUME:
			mvprintw(y,x,"%*d",column_items[COL_BID_VOLUME].width,vquotes[i].DepthMarketData.BidVolume1);
			x+=column_items[COL_BID_VOLUME].width+1;
			break;
		case COL_ASK_PRICE:
			if(vquotes[i].DepthMarketData.AskPrice1==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_ASK_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_ASK_PRICE].width,vquotes[i].precision,vquotes[i].DepthMarketData.AskPrice1);
			x+=column_items[COL_ASK_PRICE].width+1;
			break;
		case COL_ASK_VOLUME:
			mvprintw(y,x,"%*d",column_items[COL_ASK_VOLUME].width,vquotes[i].DepthMarketData.AskVolume1);
			x+=column_items[COL_ASK_VOLUME].width+1;
			break;
		case COL_HIGH_LIMIT:
			if (vquotes[i].DepthMarketData.UpperLimitPrice == DBL_MAX)
				mvprintw(y, x, "%*c", column_items[COL_HIGH_LIMIT].width, '-');
			else
				mvprintw(y, x, "%*.*f", column_items[COL_HIGH_LIMIT].width, vquotes[i].precision, vquotes[i].DepthMarketData.UpperLimitPrice);
			x += column_items[COL_HIGH_LIMIT].width + 1;
			break;
		case COL_LOW_LIMIT:
			if (vquotes[i].DepthMarketData.LowerLimitPrice == DBL_MAX)
				mvprintw(y, x, "%*c", column_items[COL_LOW_LIMIT].width, '-');
			else
				mvprintw(y, x, "%*.*f", column_items[COL_LOW_LIMIT].width, vquotes[i].precision, vquotes[i].DepthMarketData.LowerLimitPrice);
			x += column_items[COL_LOW_LIMIT].width + 1;
			break;
		case COL_OPEN:
			if(vquotes[i].DepthMarketData.OpenPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_OPEN].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_OPEN].width,vquotes[i].precision,vquotes[i].DepthMarketData.OpenPrice);
			x+=column_items[COL_OPEN].width+1;
			break;
		case COL_PREV_SETTLEMENT:
			if(vquotes[i].DepthMarketData.PreSettlementPrice ==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_PREV_SETTLEMENT].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_PREV_SETTLEMENT].width,vquotes[i].precision,vquotes[i].DepthMarketData.PreSettlementPrice);
			x+=column_items[COL_PREV_SETTLEMENT].width+1;
			break;
		case COL_TRADE_VOLUME:
			mvprintw(y,x,"%*d",column_items[COL_TRADE_VOLUME].width,vquotes[i].trade_volume);
			x+=column_items[COL_TRADE_VOLUME].width+1;
			break;
		case COL_AVERAGE_PRICE:
			if(vquotes[i].DepthMarketData.AveragePrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_AVERAGE_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_AVERAGE_PRICE].width,vquotes[i].precision,vquotes[i].DepthMarketData.AveragePrice);
			x+=column_items[COL_AVERAGE_PRICE].width+1;
			break;
		case COL_HIGH:
			if(vquotes[i].DepthMarketData.HighestPrice ==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_HIGH].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_HIGH].width,vquotes[i].precision,vquotes[i].DepthMarketData.HighestPrice);
			x+=column_items[COL_HIGH].width+1;
			break;
		case COL_LOW:
			if(vquotes[i].DepthMarketData.LowestPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_LOW].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_LOW].width,vquotes[i].precision,vquotes[i].DepthMarketData.LowestPrice);
			x+=column_items[COL_LOW].width+1;
			break;
		case COL_SETTLEMENT:
			if(vquotes[i].DepthMarketData.SettlementPrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_SETTLEMENT].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_SETTLEMENT].width,vquotes[i].precision,vquotes[i].DepthMarketData.SettlementPrice);
			x+=column_items[COL_SETTLEMENT].width+1;
			break;
		case COL_PREV_CLOSE:
			if(vquotes[i].DepthMarketData.PreClosePrice==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_PREV_CLOSE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_PREV_CLOSE].width,vquotes[i].precision,vquotes[i].DepthMarketData.PreClosePrice);
			x+=column_items[COL_PREV_CLOSE].width+1;
			break;
		case COL_OPENINT:
			mvprintw(y,x,"%*d",column_items[COL_OPENINT].width,(int)vquotes[i].DepthMarketData.OpenInterest);
			x+=column_items[COL_OPENINT].width+1;
			break;
		case COL_PREV_OPENINT:
			mvprintw(y,x,"%*d",column_items[COL_PREV_OPENINT].width,(int)vquotes[i].DepthMarketData.PreOpenInterest);
			x+=column_items[COL_PREV_OPENINT].width+1;
			break;
		case COL_DATE:
			mvprintw(y, x, "%-*s", column_items[COL_DATE].width, vquotes[i].DepthMarketData.ActionDay);
			x += column_items[COL_DATE].width + 1;
			break;
		case COL_TIME:
			mvprintw(y, x, "%-*s", column_items[COL_TIME].width, vquotes[i].DepthMarketData.UpdateTime);
			x += column_items[COL_TIME].width + 1;
			break;
		case COL_TRADE_DAY:	
			mvprintw(y, x, "%-*s", column_items[COL_TRADE_DAY].width, vquotes[i].DepthMarketData.TradingDay);
			x += column_items[COL_TRADE_DAY].width + 1;
			break;
		case COL_EXCHANGE:
			mvprintw(y, x, "%-*s", column_items[COL_EXCHANGE].width, vquotes[i].Instrument.ExchangeID);
			x += column_items[COL_EXCHANGE].width + 1;
			break;
		default:
			break;
		}
	}

	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	if(corner_win){
		corner_redraw();
	}
}

void order_display_quotation(const char *product_id)
{
	if(strcmp(vquotes[order_symbol_index].product_id,product_id)!=0)
		return;
	order_redraw();
	if(order_corner_win){
		order_corner_redraw();
	}
}
void display_column(int col)
{
	int i=0,y,x;
	for(auto iter=vcolumns.begin();iter!=vcolumns.end();iter++,i++){
		if(*iter!=col)
			continue;
		if(i<column_settings_curr_pos || i>column_settings_curr_pos+column_settings_max_lines-1)
			continue;
		y=i-column_settings_curr_pos+1;
		x=0;

		move(y,0);
		clrtoeol();
		
		if(mcolumns[*iter]==true)
			mvprintw(y,x,"*%s",column_items[*iter].name);
		else
			mvprintw(y,x," %s",column_items[*iter].name);
	}
}
	

const char *apistrerror(int e)
{
	int i;

	for(i=0;i<apierrorcount;i++){
		if(e==apierrorarray[i].error_no)
			return apierrorarray[i].error_message;
	}
	return apierror_none;
}
void display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];

	if(working_window!=WIN_MAINBOARD)
		return;
	getmaxyx(stdscr,y,x);
	struct tm *t;
	time_t tt;
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%ld]",curr_pos+curr_line,vquotes.size());
	mvprintw(y - 1, 15, "%s", status_message);
	mvprintw(y-1,x-25,"%s %s",pTradeRsp->user,tradetime);
}
void order_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_ORDER)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}

	//int pos,max_ticks;
	//if(order_symbol_index<0){
	//	pos=0;
	//	max_ticks=0;
	//}else{
	//	int precision=vquotes[order_symbol_index].precision;
	//	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	//	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	//	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	//	double buy_price=vquotes[order_symbol_index].DepthMarketData.BidPrice1;
	//	int buy_quantity=vquotes[order_symbol_index].buy_quantity;
	//	double sell_price=vquotes[order_symbol_index].DepthMarketData.AskPrice1;
	//	int sell_quantity=vquotes[order_symbol_index].sell_quantity;
	//	double close_price=vquotes[order_symbol_index].price;
	//	double prev_settle=vquotes[order_symbol_index].prev_settle;
	//	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	//	if(high_limit==DBL_MAX || high_limit==0 || low_limit==DBL_MAX || low_limit==0){
	//		pos=0;
	//		max_ticks=0;
	//	}else{
	//		if(order_curr_price==0){
	//			pos=0;
	//			order_curr_price=high_limit;
	//		}else if(order_curr_price>=high_limit+error_amount || order_curr_price<=low_limit-error_amount){
	//			pos=0;
	//			order_curr_price=high_limit;
	//		}else{
	//			pos=(high_limit-order_curr_price)/PriceTick+1+0.5;
	//		}
	//		max_ticks=(high_limit-low_limit)/PriceTick+1+0.5;
	//	}
	//}
	//
	//mvprintw(order_max_lines+2,0,"帐户:%s",order_curr_accname);

	//int buy_quantity=0,sell_quantity=0,buying_quantity=0,selling_quantity=0,canceling_buy_quantity=0,canceling_sell_quantity=0;
	//
	//std::vector<CThostFtdcOrderField>::iterator iterOrder;
	//std::vector<CThostFtdcInputOrderActionField>::iterator iterCanceling;
	//for(iterOrder=vOrders.begin();iterOrder!=vOrders.end();iterOrder++){
	//	if(strcmp(iterOrder->InvestorID,order_curr_accname)!=0 || strcmp(iterOrder->InstrumentID,vquotes[order_symbol_index].product_id)!=0 || iterOrder->OrderStatus==THOST_FTDC_OST_AllTraded || iterOrder->OrderStatus==THOST_FTDC_OST_Canceled)
	//		continue;
	//	if(iterOrder->OrderStatus==THOST_FTDC_OST_NoTradeQueueing || iterOrder->OrderStatus==THOST_FTDC_OST_PartTradedNotQueueing){
	//		if(iterOrder->Direction==THOST_FTDC_D_Buy)
	//			buy_quantity+=iterOrder->VolumeTotal;
	//		else
	//			sell_quantity+=iterOrder->VolumeTotal;
	//	}else{
	//		if(iterOrder->Direction==THOST_FTDC_D_Buy)
	//			buying_quantity+=iterOrder->VolumeTotal;
	//		else
	//			selling_quantity+=iterOrder->VolumeTotal;
	//	}
	//	
	//	// Canceling
	//	for(iterCanceling=vCancelingOrders.begin();iterCanceling!=vCancelingOrders.end();iterCanceling++){
	//		if(strcmp(iterCanceling->InstrumentID,iterOrder->InstrumentID)==0 && iterCanceling->FrontID==iterOrder->FrontID && iterCanceling->SessionID==iterOrder->SessionID && strcmp(iterCanceling->OrderRef,iterOrder->OrderRef)==0){
	//			if(iterOrder->Direction==THOST_FTDC_D_Buy)
	//				canceling_buy_quantity+=iterOrder->VolumeTotal;
	//			else
	//				canceling_sell_quantity+=iterOrder->VolumeTotal;
	//			break;
	//		}
	//	}
	//}
	//char strbuyorders[100],strsellorders[100];
	//memset(strbuyorders,0x00,sizeof(strbuyorders));
	//memset(strsellorders,0x00,sizeof(strsellorders));
	//if(buy_quantity>0 || buying_quantity>0 || canceling_buy_quantity>0){
	//	if(buy_quantity>0){
	//		sprintf(strbuyorders,"%d",buy_quantity);
	//	}
	//	if(buying_quantity>0){
	//		sprintf(strbuyorders+strlen(strbuyorders),"(%d",buying_quantity);
	//		if(canceling_buy_quantity>0){
	//			sprintf(strbuyorders+strlen(strbuyorders),"/-%d",canceling_buy_quantity);
	//		}
	//		strcat(strbuyorders,")");
	//	}else{
	//		if(canceling_buy_quantity>0){
	//			sprintf(strbuyorders+strlen(strbuyorders),"(-%d)",canceling_buy_quantity);
	//		}
	//	}
	//}
	//if(strlen(strbuyorders)==0)
	//	strcpy(strbuyorders,"0");

	//if(sell_quantity>0 || selling_quantity>0 || canceling_sell_quantity>0){
	//	if(sell_quantity>0){
	//		sprintf(strsellorders,"%d",sell_quantity);
	//	}
	//	if(selling_quantity>0){
	//		sprintf(strsellorders+strlen(strsellorders),"(%d",selling_quantity);
	//		if(canceling_sell_quantity>0){
	//			sprintf(strsellorders+strlen(strsellorders),"/-%d",canceling_sell_quantity);
	//		}
	//		strcat(strsellorders,")");
	//	}else{
	//		if(canceling_sell_quantity>0){
	//			sprintf(strsellorders+strlen(strsellorders),"(-%d)",canceling_sell_quantity);
	//		}
	//	}
	//}
	//if(strlen(strsellorders)==0)
	//	strcpy(strsellorders,"0");
	move(y - 1, 0);
	clrtoeol();
	mvprintw(y - 1, 15, "%s", status_message);
	mvprintw(y - 1, x - 25, "%s %s", pTradeRsp->user, tradetime);
	//mvprintw(y-1,x-25,"%s,%s",strbuyorders,strsellorders);
}
void column_settings_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_COLUMN_SETTINGS)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,x-25,"%s %s", pTradeRsp->user,tradetime);
}
void symbol_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_SYMBOL)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,x-25,"%s %s", pTradeRsp->user,tradetime);
}

void init_screen()
{
	int y,x;

	if(working_window!=WIN_MAINBOARD)
		return;

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
	for(size_t i=0;i<vquotes.size();i++)
		display_quotation(i);
	display_status();
	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}

void refresh_screen()
{
	int y,x;

	if(working_window!=WIN_MAINBOARD)
		return;
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
	for(size_t i=0;i<vquotes.size();i++)
		display_quotation(i);
	display_status();
	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}
void order_refresh_screen()
{
	int y,x;
	
	if(working_window!=WIN_ORDER)
		return;
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
	order_max_lines=y-3;
	order_is_moving=0;
	order_redraw();
}
void order_goto_file_top()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	order_page_top_price=high_limit;
	order_curr_price=high_limit;
	
	order_redraw();
}
void order_goto_file_bottom()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	order_curr_price=low_limit;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	if(order_page_top_price>=order_curr_price+PriceTick*(order_max_lines-1)+error_amount)
			order_page_top_price=order_curr_price+PriceTick*(order_max_lines-1);
	
	order_redraw();
}

void order_goto_page_top()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	order_curr_price=order_page_top_price;
	order_redraw();
}
void order_goto_page_bottom()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	order_curr_price=order_page_top_price-PriceTick*(order_max_lines-1);
	if(order_curr_price<=low_limit-error_amount)
		order_curr_price=low_limit;
	order_redraw();
}
void order_goto_page_middle()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	if(order_page_top_price-PriceTick*(order_max_lines-1)<=low_limit-error_amount)
		order_curr_price=(order_page_top_price+low_limit)/2;
	else
		order_curr_price=order_page_top_price-PriceTick*order_max_lines/2;
	order_redraw();
}

void order_scroll_forward_1_line()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}else{
		if(order_page_top_price>=low_limit+error_amount)
			order_page_top_price-=PriceTick;
	}
	if(order_curr_price==DBL_MAX){
		order_curr_price=high_limit;
	}else if(order_curr_price>=high_limit+error_amount || order_curr_price<=low_limit-error_amount){
		order_curr_price=high_limit;
	}else{
		if(order_curr_price>=order_page_top_price+error_amount)
			order_curr_price=order_page_top_price;
	}
	
	order_redraw();
}
void order_scroll_backward_1_line()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}else if(fabs(order_page_top_price-high_limit)<error_amount){
		order_page_top_price=high_limit;
	}else{
		order_page_top_price+=PriceTick;
	}
	if(order_curr_price==DBL_MAX){
		order_curr_price=high_limit;
	}else if(order_curr_price>=high_limit+error_amount || order_curr_price<=low_limit-error_amount){
		order_curr_price=high_limit;
	}else{
		if(order_curr_price<=order_page_top_price-(order_max_lines-1)*PriceTick-error_amount)
			order_curr_price=order_page_top_price-(order_max_lines-1)*PriceTick;
	}
	order_redraw();
}
int order_move_forward_1_page()
{
	int i;
	for(i=0;i<order_max_lines;i++)
		order_scroll_forward_1_line();
	return 0;
}
int order_move_backward_1_page()
{
	int i;
	for(i=0;i<order_max_lines;i++)
		order_scroll_backward_1_line();
	
	return 0;
}
int order_move_forward_half_page()
{
	int i;
	for(i=0;i<order_max_lines/2;i++)
		order_scroll_forward_1_line();
	return 0;
}
int order_move_backward_half_page()
{
	int i;
	for(i=0;i<order_max_lines/2;i++)
		order_scroll_backward_1_line();
	return 0;
}

void order_centralize_current_price()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double close_price=vquotes[order_symbol_index].DepthMarketData.LastPrice;
	double prev_close = vquotes[order_symbol_index].DepthMarketData.PreClosePrice;
	double prev_settle=vquotes[order_symbol_index].DepthMarketData.PreSettlementPrice;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	order_curr_price = close_price;

	if(order_curr_price == DBL_MAX){
		// centralize to prev_settle
		order_curr_price= prev_close;
	}
	if (order_curr_price == DBL_MAX) {
		// centralize to prev_settle
		order_curr_price = prev_settle;
	}

	if((order_page_top_price=order_curr_price+order_max_lines/2*PriceTick)>=high_limit+error_amount)
		order_page_top_price=high_limit;
	order_redraw();
}
int order_refresh_quote()
{
	int i;
	
	i=order_symbol_index;
	int precision=vquotes[i].precision;
	double high_limit=vquotes[i].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[i].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[i].Instrument.PriceTick;
	double buy_price=vquotes[i].DepthMarketData.BidPrice1;
	int buy_quantity=vquotes[i].DepthMarketData.BidVolume1;
	double sell_price=vquotes[i].DepthMarketData.AskPrice1;
	int sell_quantity=vquotes[i].DepthMarketData.AskVolume1;
	double close_price=vquotes[i].DepthMarketData.LastPrice;
	int max_ticks=(high_limit-low_limit)/PriceTick+1+0.5;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(max_ticks==1)
		return 0;
	if(order_curr_line!=0)
		mvchgat(order_curr_line+1,0,-1,A_NORMAL,0,NULL);
	for(i=0;i<order_max_lines;i++){
		if(high_limit-PriceTick*(i+order_curr_pos)<high_limit+error_amount && high_limit-PriceTick*(i+order_curr_pos)>low_limit-error_amount){
			move(i+2,0);
			clrtoeol();
			mvprintw(i+2,22,"%10.*f",precision,high_limit-PriceTick*(i+order_curr_pos));
		}
	}
	
	// Ask Volume
	if(sell_quantity>0){
		order_curr_pos_ask=(high_limit-sell_price)/PriceTick+0.5;
		if(order_curr_pos_ask>=order_curr_pos && order_curr_pos_ask<=order_curr_pos+order_max_lines-1)
			mvprintw(order_curr_pos_ask-order_curr_pos+2,11,"%10d",sell_quantity);
	}
	
	// Bid Volume
	if(buy_quantity>0){
		order_curr_pos_bid=(high_limit-buy_price)/PriceTick+0.5;
		if(order_curr_pos_bid>=order_curr_pos && order_curr_pos_bid<=order_curr_pos+order_max_lines-1)
			mvprintw(order_curr_pos_bid-order_curr_pos+2,33,"%10d",buy_quantity);
	}
	mvchgat(order_curr_line+1,0,-1,A_REVERSE,0,NULL);
	if((high_limit-close_price)/PriceTick-order_curr_pos+1+0.5==order_curr_line)
		mvchgat(order_curr_line+1,22,11,A_NORMAL,0,NULL);
	else if((high_limit-close_price)/PriceTick-order_curr_pos+1+0.5>=1 && (high_limit-close_price)/PriceTick-order_curr_pos+1+0.5<=order_max_lines)
		mvchgat((high_limit-close_price)/PriceTick-order_curr_pos+1+1+0.5,22,11,A_REVERSE,0,NULL);
	
	return 0;
}

int order_goto_price(double price)
{
	int i;
	
	i=order_symbol_index;
	int precision=vquotes[i].precision;
	double high_limit=vquotes[i].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[i].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[i].Instrument.PriceTick;
	double buy_price = vquotes[i].DepthMarketData.BidPrice1;
	int buy_quantity = vquotes[i].DepthMarketData.BidVolume1;
	double sell_price = vquotes[i].DepthMarketData.AskPrice1;
	int sell_quantity = vquotes[i].DepthMarketData.AskVolume1;
	double close_price=vquotes[i].DepthMarketData.LastPrice;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	int max_ticks=(high_limit-low_limit)/PriceTick+1+0.5;
	double curr_price;

	if(max_ticks==1)
		return 0;
	if(order_curr_line!=0){
		mvchgat(order_curr_line+1,0,-1,A_NORMAL,0,NULL);
		curr_price=high_limit-(order_curr_pos+order_curr_line-1)*PriceTick;
	}
	if((high_limit-price)/PriceTick+0.5<order_curr_pos){
		order_curr_pos=(high_limit-price)/PriceTick+0.5;
		for(i=0;i<order_max_lines;i++){
			if(high_limit-PriceTick*(i+order_curr_pos)<high_limit+error_amount && high_limit-PriceTick*(i+order_curr_pos)>low_limit-error_amount){
				move(i+2,0);
				clrtoeol();
				mvprintw(i+2,22,"%10.*f",precision,high_limit-PriceTick*(i+order_curr_pos));
			}
		}
		order_curr_line=1;
	}else if((high_limit-price)/PriceTick+0.5>order_curr_pos+order_max_lines-1){
		order_curr_pos=(high_limit-price)/PriceTick+0.5;
		for(i=0;i<order_max_lines;i++){
			if(high_limit-PriceTick*(i+order_curr_pos)<high_limit+error_amount && high_limit-PriceTick*(i+order_curr_pos)>low_limit-error_amount){
				move(i+2,0);
				clrtoeol();
				mvprintw(i+2,22,"%10.*f",precision,high_limit-PriceTick*(i+order_curr_pos));
			}
		}
		order_curr_line=order_max_lines;
	}else{
		for(i=0;i<order_max_lines;i++){
			if(high_limit-PriceTick*(i+order_curr_pos)<high_limit+error_amount && high_limit-PriceTick*(i+order_curr_pos)>low_limit-error_amount){
				move(i+2,0);
				clrtoeol();
				mvprintw(i+2,22,"%10.*f",precision,high_limit-PriceTick*(i+order_curr_pos));
			}
		}
		order_curr_line=(high_limit-price)/PriceTick-order_curr_pos+1+0.5;
	}
	
	// Ask Volume
	if(sell_quantity>0){
		order_curr_pos_ask=(high_limit-sell_price)/PriceTick+0.5;
		if(order_curr_pos_ask>=order_curr_pos && order_curr_pos_ask<=order_curr_pos+order_max_lines-1)
			mvprintw(order_curr_pos_ask-order_curr_pos+2,11,"%10d",sell_quantity);
	}
	
	// Bid Volume
	if(buy_quantity>0){
		order_curr_pos_bid=(high_limit-buy_price)/PriceTick+0.5;
		if(order_curr_pos_bid>=order_curr_pos && order_curr_pos_bid<=order_curr_pos+order_max_lines-1)
			mvprintw(order_curr_pos_bid-order_curr_pos+2,33,"%10d",buy_quantity);
	}
	mvchgat(order_curr_line+1,0,-1,A_REVERSE,0,NULL);
	if(fabs(close_price-price)<error_amount){
		mvchgat(order_curr_line+1,22,11,A_NORMAL,0,NULL);
		mvchgat(order_curr_line+1,22,11,A_BLINK,0,NULL);
	}

	return 0;
}

void order_redraw()
{
	erase();
	order_display_title();
	order_display_prices();
	order_display_bid_ask();
	order_display_orders();
	order_display_status();
	order_display_focus();
}
void order_display_prices()
{
	if(order_symbol_index<0)
		return;

	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	int i;
	for(i=0;i<order_max_lines;i++){
		if(order_page_top_price-PriceTick*i>=low_limit-error_amount)
			mvprintw(i+2,22,"%10.*f",precision,order_page_top_price-PriceTick*i);
	}
}
void order_display_orders()
{
	if(order_symbol_index<0)
		return;
	
	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double buy_price = vquotes[order_symbol_index].DepthMarketData.BidPrice1;
	int buy_quantity = vquotes[order_symbol_index].DepthMarketData.BidVolume1;
	double sell_price = vquotes[order_symbol_index].DepthMarketData.AskPrice1;
	int sell_quantity = vquotes[order_symbol_index].DepthMarketData.AskVolume1;
	double close_price=vquotes[order_symbol_index].DepthMarketData.LastPrice;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;

	int i;
	for(i=0;i<order_max_lines;i++){
		if(order_page_top_price-PriceTick*i>=low_limit-error_amount)
			order_display_orders_at_price(order_page_top_price-PriceTick*i);
	}
}

void order_display_orders_at_price(double price)
{
	if(order_symbol_index<0)
		return;
	
	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(price>=high_limit+error_amount || price<=low_limit-error_amount)	//不显示范围外的报单
		return;

	int buy_quantity=0,sell_quantity=0,buying_quantity=0,selling_quantity=0,canceling_buy_quantity=0,canceling_sell_quantity=0;
	int nLine;

	std::vector<CThostFtdcOrderField>::iterator iter;
	std::vector<CThostFtdcInputOrderActionField>::iterator iterCanceling;
	for(iter=vOrders.begin();iter!=vOrders.end();iter++){
		if(strcmp(iter->InvestorID,order_curr_accname)!=0 ||strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)!=0 || iter->OrderStatus==THOST_FTDC_OST_AllTraded || iter->OrderStatus==THOST_FTDC_OST_Canceled)
			continue;
		if(fabs(iter->LimitPrice-price)<error_amount){
			if(iter->OrderStatus==THOST_FTDC_OST_NoTradeQueueing || iter->OrderStatus== THOST_FTDC_OST_PartTradedQueueing){
				if(iter->Direction==THOST_FTDC_D_Buy)
					buy_quantity+=iter->VolumeTotal;
				else
					sell_quantity+=iter->VolumeTotal;
			}else{
				if(iter->Direction==THOST_FTDC_D_Buy)
					buying_quantity+=iter->VolumeTotal;
				else
					selling_quantity+=iter->VolumeTotal;
			}
			
			// Canceling
			for(iterCanceling=vCancelingOrders.begin();iterCanceling!=vCancelingOrders.end();iterCanceling++){
				if(strcmp(iterCanceling->InstrumentID,iter->InstrumentID)==0 && iterCanceling->FrontID==iter->FrontID && iterCanceling->SessionID==iter->SessionID && strcmp(iterCanceling->OrderRef,iter->OrderRef)==0){
					if(iter->Direction==THOST_FTDC_D_Buy)
						canceling_buy_quantity+=iter->VolumeTotal;
					else
						canceling_sell_quantity+=iter->VolumeTotal;
					break;
				}
			}
		}
	}
	char strbuyorders[100],strsellorders[100];
	memset(strbuyorders,0x00,sizeof(strbuyorders));
	memset(strsellorders,0x00,sizeof(strsellorders));
	if(buy_quantity>0 || buying_quantity>0 || canceling_buy_quantity>0){
		nLine=(order_page_top_price-price)/PriceTick+1+0.5;
		if(nLine>=1 && nLine<=order_max_lines){
			if(buy_quantity>0){
				sprintf(strbuyorders,"%d",buy_quantity);
			}
			if(buying_quantity>0){
				sprintf(strbuyorders+strlen(strbuyorders),"(%d",buying_quantity);
				if(canceling_buy_quantity>0){
					sprintf(strbuyorders+strlen(strbuyorders),"/-%d",canceling_buy_quantity);
				}
				strcat(strbuyorders,")");
			}else{
				if(canceling_buy_quantity>0){
					sprintf(strbuyorders+strlen(strbuyorders),"(-%d)",canceling_buy_quantity);
				}
			}
			mvprintw(nLine+1,0,"%10s",strbuyorders);
		}
	}
	if(sell_quantity>0 || selling_quantity>0 || canceling_sell_quantity>0){
		nLine=(order_page_top_price-price)/PriceTick+1+0.5;
		if(nLine>=1 && nLine<=order_max_lines){
			if(sell_quantity>0){
				sprintf(strsellorders,"%d",sell_quantity);
			}
			if(selling_quantity>0){
				sprintf(strsellorders+strlen(strsellorders),"(%d",selling_quantity);
				if(canceling_sell_quantity>0){
					sprintf(strsellorders+strlen(strsellorders),"/-%d",canceling_sell_quantity);
				}
				strcat(strsellorders,")");
			}else{
				if(canceling_sell_quantity>0){
					sprintf(strsellorders+strlen(strsellorders),"(-%d)",canceling_sell_quantity);
				}
			}
			mvprintw(nLine+1,44,"%10s",strsellorders);
		}
	}
}

void order_display_bid_ask()
{
	if(order_symbol_index<0)
		return;
	CThostFtdcDepthMarketDataField& DepthMarketData = vquotes[order_symbol_index].DepthMarketData;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	int nLine;
		
	if(DepthMarketData.UpperLimitPrice==DBL_MAX || DepthMarketData.LowerLimitPrice==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price= DepthMarketData.UpperLimitPrice;
	}else if(order_page_top_price>= DepthMarketData.UpperLimitPrice +error_amount || order_page_top_price<= DepthMarketData.LowerLimitPrice -error_amount){
		order_page_top_price= DepthMarketData.UpperLimitPrice;
	}
	if(DepthMarketData.BidVolume1!=0){
		nLine=(order_page_top_price- DepthMarketData.BidPrice1)/PriceTick+1+0.5;
		if(nLine>=1 && nLine<=order_max_lines){
			mvprintw(nLine+1,11,"%10d", DepthMarketData.BidVolume1);
			//mvchgat(nLine+1,11,10,A_REVERSE,0,NULL);
		}
	}
	if(DepthMarketData.AskVolume1 !=0){
		nLine=(order_page_top_price- DepthMarketData.AskPrice1)/PriceTick+1+0.5;
		if(nLine>=1 && nLine<=order_max_lines){
			mvprintw(nLine+1,33,"%10d", DepthMarketData.AskVolume1);
			//mvchgat(nLine+1,33,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.BidVolume2 != 0) {
		nLine = (order_page_top_price - DepthMarketData.BidPrice2) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 11, "%10d", DepthMarketData.BidVolume2);
			//mvchgat(nLine+1,11,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.AskVolume2 != 0) {
		nLine = (order_page_top_price - DepthMarketData.AskPrice2) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 33, "%10d", DepthMarketData.AskVolume2);
			//mvchgat(nLine+1,33,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.BidVolume3 != 0) {
		nLine = (order_page_top_price - DepthMarketData.BidPrice3) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 11, "%10d", DepthMarketData.BidVolume3);
			//mvchgat(nLine+1,11,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.AskVolume3 != 0) {
		nLine = (order_page_top_price - DepthMarketData.AskPrice3) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 33, "%10d", DepthMarketData.AskVolume3);
			//mvchgat(nLine+1,33,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.BidVolume4 != 0) {
		nLine = (order_page_top_price - DepthMarketData.BidPrice4) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 11, "%10d", DepthMarketData.BidVolume4);
			//mvchgat(nLine+1,11,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.AskVolume4 != 0) {
		nLine = (order_page_top_price - DepthMarketData.AskPrice4) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 33, "%10d", DepthMarketData.AskVolume4);
			//mvchgat(nLine+1,33,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.BidVolume5 != 0) {
		nLine = (order_page_top_price - DepthMarketData.BidPrice5) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 11, "%10d", DepthMarketData.BidVolume5);
			//mvchgat(nLine+1,11,10,A_REVERSE,0,NULL);
		}
	}
	if (DepthMarketData.AskVolume5 != 0) {
		nLine = (order_page_top_price - DepthMarketData.AskPrice5) / PriceTick + 1 + 0.5;
		if (nLine >= 1 && nLine <= order_max_lines) {
			mvprintw(nLine + 1, 33, "%10d", DepthMarketData.AskVolume5);
			//mvchgat(nLine+1,33,10,A_REVERSE,0,NULL);
		}
	}
}

void order_display_focus()
{
	if(order_symbol_index<0)
		return;

	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double buy_price=vquotes[order_symbol_index].DepthMarketData.BidPrice1;
	int buy_quantity=vquotes[order_symbol_index].DepthMarketData.BidVolume1;
	double sell_price=vquotes[order_symbol_index].DepthMarketData.AskPrice1;
	int sell_quantity=vquotes[order_symbol_index].DepthMarketData.AskVolume1;
	double close_price=vquotes[order_symbol_index].DepthMarketData.LastPrice;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	int nLine;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	if(order_curr_price==DBL_MAX){
		order_curr_price=high_limit;
	}else if(order_curr_price>=high_limit+error_amount || order_curr_price<=low_limit-error_amount){
		order_curr_price=high_limit;
	}
	order_curr_line=(order_page_top_price-order_curr_price)/PriceTick+1+0.5;
	if(order_curr_col==0){
		mvchgat(order_curr_line+1,0,10,A_REVERSE,0,NULL);
	}else{
		mvchgat(order_curr_line+1,44,10,A_REVERSE,0,NULL);
	}
	//mvchgat(order_curr_line+1,0,-1,A_REVERSE,0,NULL);
	if(close_price!=DBL_MAX){
		nLine=(order_page_top_price-close_price)/PriceTick+1+0.5;
		if(nLine==order_curr_line)
			mvchgat(order_curr_line+1,22,10,A_UNDERLINE,0,NULL);
		else if(nLine>=1 && nLine<=order_max_lines)
			mvchgat(nLine+1,22,10,A_UNDERLINE,0,NULL);
	}

	//for(int i=0;i<order_max_lines;i++){
	//	mvchgat(i+2,11,10,A_REVERSE,0,NULL);
	//	mvchgat(i+2,33,10,A_REVERSE,0,NULL);
	//}
}
void order_move_orders()
{
	order_moving_at_price=order_curr_price;
	order_is_moving=1;
}

void order_move_complete()
{
	if(order_symbol_index<0)
		return;

	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double buy_price = vquotes[order_symbol_index].DepthMarketData.BidPrice1;
	int buy_quantity = vquotes[order_symbol_index].DepthMarketData.BidVolume1;
	double sell_price = vquotes[order_symbol_index].DepthMarketData.AskPrice1;
	int sell_quantity = vquotes[order_symbol_index].DepthMarketData.AskVolume1;
	double close_price=vquotes[order_symbol_index].DepthMarketData.LastPrice;
	double prev_settle=vquotes[order_symbol_index].DepthMarketData.PreSettlementPrice;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;

	CThostFtdcTraderApi *pTradeReq;
	
	pTradeReq= pTradeRsp->m_pTradeReq;

	double price;
	std::vector<CThostFtdcOrderField>::iterator iter;
	std::vector<CThostFtdcInputOrderActionField>::iterator iterCanceling;
	for(iter=vOrders.begin();iter!=vOrders.end();iter++){
		if(strcmp(iter->InvestorID,pTradeRsp->user)!=0 || strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)!=0 || iter->OrderStatus==THOST_FTDC_OST_AllTraded || iter->OrderStatus==THOST_FTDC_OST_Canceled)
			continue;
		for(iterCanceling=vCancelingOrders.begin();iterCanceling!=vCancelingOrders.end();iterCanceling++){
			if(strcmp(iterCanceling->InstrumentID,iter->InstrumentID)==0 && iterCanceling->FrontID==iter->FrontID && iterCanceling->SessionID==iter->SessionID && strcmp(iterCanceling->OrderRef,iter->OrderRef)==0)
				break;
		}
		if(iterCanceling!=vCancelingOrders.end())	// 不重复发送撤单指令
			continue;
		if((order_curr_col==0 && iter->Direction!=THOST_FTDC_D_Buy) || (order_curr_col==1 && iter->Direction!=THOST_FTDC_D_Sell))
			continue;
		price=order_moving_at_price;
		if(fabs(iter->LimitPrice-price)<0.000001){
			// 改限价单
			CThostFtdcInputOrderActionField Req{};
			memset(&Req,0x00,sizeof(Req));
			strncpy(Req.BrokerID,iter->BrokerID,sizeof(Req.BrokerID)-1);
			strncpy(Req.InvestorID,iter->InvestorID,sizeof(Req.InvestorID)-1);
			strncpy(Req.ExchangeID,iter->ExchangeID,sizeof(Req.ExchangeID)-1);
			strcpy(Req.InstrumentID,iter->InstrumentID);
			strcpy(Req.OrderRef,iter->OrderRef);
			Req.OrderActionRef=pTradeRsp->TradeOrderRef++;
			Req.FrontID=iter->FrontID;
			Req.SessionID=iter->SessionID;
			strcpy(Req.OrderSysID,iter->OrderSysID);
			Req.ActionFlag=THOST_FTDC_AF_Delete;
			Req.LimitPrice=price;

			if(pTradeRsp->m_pTradeReq->ReqOrderAction(&Req,0)<0){
				// to do ...
				continue;
			}

			// 记录订单
			vCancelingOrders.push_back(Req);

			CThostFtdcOrderField Order{};
			memset(&Order,0x00,sizeof(Order));
			strncpy(Order.InvestorID,iter->InvestorID,sizeof(Order.InvestorID)-1);
			strncpy(Order.ExchangeID,iter->ExchangeID,sizeof(Order.ExchangeID)-1);
			strcpy(Order.InstrumentID,iter->InstrumentID);
			strcpy(Order.OrderRef,iter->OrderRef);
			Order.FrontID=iter->FrontID;
			Order.SessionID=iter->SessionID;
			strcpy(Order.OrderSysID,iter->OrderSysID);
			Order.LimitPrice=order_curr_price;
			pTradeRsp->m_mMovingOrders.push_back(Order);
		}
	}

	order_is_moving=0;
}
void order_buy_at_market(unsigned int n)
{
	if(order_symbol_index<0)
		return;

	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	order_buy_at_limit_price(high_limit,n);
	order_redraw();
}
void order_sell_at_market(unsigned int n)
{
	if(order_symbol_index<0)
		return;

	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	order_sell_at_limit_price(low_limit,n);
	order_redraw();
}
void order_buy_at_limit(unsigned int n)
{
	order_buy_at_limit_price(order_curr_price,n);
	order_redraw();
}
void order_buy_at_limit_price(double price,unsigned int n)
{
	if(order_symbol_index<0)
		return;

	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	// 自动开平（可能分成三笔：开仓、平今、平仓）
	unsigned int nOpen=0;
	unsigned int nClose=0;
	unsigned int nCloseToday=0;

	getOrderOffsetFlag(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Buy,n,nOpen,nClose,nCloseToday); // 自动开平
	// 报单顺序依次为：平今、平仓、开仓
	if(nCloseToday){
		// 平今
		OrderInsert(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Buy,THOST_FTDC_OF_CloseToday,price,nCloseToday);
	}
	if(nClose){
		// 平仓
		OrderInsert(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Buy,THOST_FTDC_OF_Close,price,nClose);
	}
	if(nOpen){
		// 开仓
		OrderInsert(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Buy,THOST_FTDC_OF_Open,price,nOpen);
	}

// 	vInputingOrders.push_back(Req);
}
void order_sell_at_limit(unsigned int n)
{
	order_sell_at_limit_price(order_curr_price,n);
	order_redraw();
}
void order_revert_at_limit()
{
	int nPosi=0,nBuyPosi=0,nSellPosi=0;

	std::vector<stPosition_t>::iterator iter;
	for(iter=vPositions.begin();iter!=vPositions.end();iter++){
		if(strcmp(iter->AccID,order_curr_accname)==0 && strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)==0)
			break;
	}
	if(iter!=vPositions.end()){
		nPosi=iter->Volume;
		nBuyPosi=iter->BuyVolume;
		nSellPosi=iter->SellVolume;
	}
	if(nBuyPosi-nSellPosi>0)
		order_sell_at_limit_price(order_curr_price,(nBuyPosi-nSellPosi)*2);
	else
		order_buy_at_limit_price(order_curr_price,(nSellPosi-nBuyPosi)*2);
	order_redraw();
}
void order_revert_at_market()
{
	if(order_symbol_index<0)
		return;

	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	int nPosi=0,nBuyPosi=0,nSellPosi=0;

	std::vector<stPosition_t>::iterator iter;
	for(iter=vPositions.begin();iter!=vPositions.end();iter++){
		if(strcmp(iter->AccID,order_curr_accname)==0 && strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)==0)
			break;
	}
	if(iter!=vPositions.end()){
		nPosi=iter->Volume;
		nBuyPosi=iter->BuyVolume;
		nSellPosi=iter->SellVolume;
	}
	if(nBuyPosi-nSellPosi>0)
		order_sell_at_limit_price(low_limit,(nBuyPosi-nSellPosi)*2);
	else
		order_buy_at_limit_price(high_limit,(nSellPosi-nBuyPosi)*2);
	order_redraw();
}

void order_sell_at_limit_price(double price,unsigned int n)
{
	if(order_symbol_index<0)
		return;

	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double buy_price = vquotes[order_symbol_index].DepthMarketData.BidPrice1;
	int buy_quantity = vquotes[order_symbol_index].DepthMarketData.BidVolume1;
	double sell_price = vquotes[order_symbol_index].DepthMarketData.AskPrice1;
	int sell_quantity = vquotes[order_symbol_index].DepthMarketData.AskVolume1;
	double close_price=vquotes[order_symbol_index].DepthMarketData.LastPrice;
	double prev_settle=vquotes[order_symbol_index].DepthMarketData.PreSettlementPrice;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;

	// 自动开平（可能分成三笔：开仓、平今、平仓）
	unsigned int nOpen=0;
	unsigned int nClose=0;
	unsigned int nCloseToday=0;

	getOrderOffsetFlag(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Sell,n,nOpen,nClose,nCloseToday); // 自动开平
	// 报单顺序依次为：平今、平仓、开仓
	if(nCloseToday){
		// 平今
		OrderInsert(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Sell,THOST_FTDC_OF_CloseToday,price,nCloseToday);
	}
	if(nClose){
		// 平仓
		OrderInsert(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Sell,THOST_FTDC_OF_Close,price,nClose);
	}
	if(nOpen){
		// 开仓
		OrderInsert(vquotes[order_symbol_index].product_id,THOST_FTDC_D_Sell,THOST_FTDC_OF_Open,price,nOpen);
	}

// 	vInputingOrders.push_back(Req);
}
int OrderInsert(const char* InstrumentID,char BSFlag,char OCFlag,double Price,unsigned int Qty)
{
	CThostFtdcTraderApi *pTradeReq;
	
	pTradeReq= pTradeRsp->m_pTradeReq;

	std::vector<quotation_t>::iterator iter_quot;
	for(iter_quot=vquotes.begin();iter_quot!=vquotes.end();iter_quot++){
		if(strcmp(iter_quot->product_id,InstrumentID)==0)
			break;
	}
	if(iter_quot==vquotes.end())
		return -1;

	CThostFtdcInputOrderField Req{};
			
	memset(&Req,0x00,sizeof(Req));
	strncpy(Req.BrokerID,pTradeRsp->broker,sizeof(Req.BrokerID)-1);
	strncpy(Req.UserID, pTradeRsp->user, sizeof(Req.UserID) - 1);
	strncpy(Req.InvestorID,pTradeRsp->user,sizeof(Req.InvestorID)-1);
	strcpy(Req.InstrumentID,InstrumentID);
	strcpy(Req.ExchangeID, iter_quot->exchange_id);
	Req.Direction=BSFlag;
	Req.CombOffsetFlag[0]=OCFlag;
	Req.CombHedgeFlag[0]=THOST_FTDC_HF_Speculation;
	Req.VolumeTotalOriginal=Qty;
	Req.LimitPrice=Price;
	Req.OrderPriceType=THOST_FTDC_OPT_LimitPrice;
	sprintf(Req.OrderRef,"%d",pTradeRsp->TradeOrderRef++);
	Req.TimeCondition=THOST_FTDC_TC_GFD;
	if(Req.OrderPriceType==THOST_FTDC_OPT_AnyPrice)
		Req.TimeCondition=THOST_FTDC_TC_IOC;
	Req.VolumeCondition=THOST_FTDC_VC_AV;
	Req.MinVolume=1;
	Req.ContingentCondition=THOST_FTDC_CC_Immediately;
	Req.ForceCloseReason=THOST_FTDC_FCC_NotForceClose;
	Req.IsAutoSuspend=0;
	Req.UserForceClose=0;
	if(pTradeReq->ReqOrderInsert(&Req,pTradeRsp->m_nTradeRequestID++)<0)
		return -1;

	CThostFtdcOrderField Order{};
	memset(&Order,0x00,sizeof(Order));
	strcpy(Order.InstrumentID,Req.InstrumentID);
	strcpy(Order.BrokerID,Req.BrokerID);
	strcpy(Order.InvestorID,Req.InvestorID);
	strcpy(Order.ExchangeID,iter_quot->exchange_id);
	Order.FrontID=pTradeRsp->TradeFrontID;
	Order.SessionID=pTradeRsp->TradeSessionID;
	strcpy(Order.OrderRef,Req.OrderRef);
	Order.Direction=Req.Direction;
	Order.CombHedgeFlag[0]=Req.CombHedgeFlag[0];
	Order.CombOffsetFlag[0]=Req.CombOffsetFlag[0];
	Order.VolumeTotalOriginal=Req.VolumeTotalOriginal;
	Order.VolumeTotal=Req.VolumeTotalOriginal;
	Order.VolumeTraded=0;
	Order.LimitPrice=Req.LimitPrice;
	Order.OrderPriceType=Req.OrderPriceType;
	Order.OrderStatus=THOST_FTDC_OST_Unknown;
	struct tm *t;
	time_t tt;
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(Order.InsertTime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	vOrders.push_back(Order);

	std::vector<stPosition_t>::iterator iter;
	for(iter=vPositions.begin();iter!=vPositions.end();iter++){
		if(strcmp(iter->AccID,Req.InvestorID)==0 && strcmp(iter->InstrumentID,Req.InstrumentID)==0){
			if(Req.Direction==THOST_FTDC_D_Sell){
				if(Req.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
					if(Req.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iter->BuyVolume-iter->TodayBuyVolume)==0)
						iter->TodayFrozenBuyVolume+=Req.VolumeTotalOriginal;
					iter->FrozenBuyVolume+=Req.VolumeTotalOriginal;
				}
				iter->SellingVolume+=Req.VolumeTotalOriginal;
			}else{
				if(Req.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
					if(Req.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iter->SellVolume-iter->TodaySellVolume)==0)
						iter->TodayFrozenSellVolume+=Req.VolumeTotalOriginal;
					iter->FrozenSellVolume+=Req.VolumeTotalOriginal;
				}
				iter->BuyingVolume+=Req.VolumeTotalOriginal;
			}
			break;
		}
	}
	if(iter==vPositions.end()){
		stPosition_t Posi;
		memset(&Posi,0x00,sizeof(Posi));
		strcpy(Posi.InstrumentID,Req.InstrumentID);
		strcpy(Posi.BrokerID,Req.BrokerID);
		strcpy(Posi.AccID,Req.InvestorID);
		strcpy(Posi.ExchangeID,iter_quot->exchange_id);
		if(Req.Direction==THOST_FTDC_D_Sell){
			if(Req.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
				if(Req.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
					Posi.TodayFrozenBuyVolume+=Req.VolumeTotalOriginal;
				Posi.FrozenBuyVolume+=Req.VolumeTotalOriginal;
			}
			Posi.SellingVolume+=Req.VolumeTotalOriginal;
		}else{
			if(Req.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
				if(Req.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
					Posi.TodayFrozenSellVolume+=Req.VolumeTotalOriginal;
				Posi.FrozenSellVolume+=Req.VolumeTotalOriginal;
			}
			Posi.BuyingVolume+=Req.VolumeTotalOriginal;
		}
		mPositionIndex[Posi.InstrumentID] = vPositions.size();
		vPositions.push_back(Posi);
	}


	return 0;
}
void order_cancel_orders()
{
	order_cancel_orders_at_price(order_curr_price);
	order_redraw();
}
void order_cancel_orders_at_price(double price)
{
	if(order_symbol_index<0)
		return;
	
	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;

	std::vector<CThostFtdcOrderField>::iterator iter;
	std::vector<CThostFtdcInputOrderActionField>::iterator iterCanceling;
	for(iter=vOrders.begin();iter!=vOrders.end();iter++){
		if(strcmp(iter->InvestorID,order_curr_accname)!=0 || strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)!=0 || iter->OrderStatus==THOST_FTDC_OST_AllTraded || iter->OrderStatus==THOST_FTDC_OST_Canceled)
			continue;
		if((order_curr_col==0 && iter->Direction!=THOST_FTDC_D_Buy) || (order_curr_col==1 && iter->Direction!=THOST_FTDC_D_Sell))
			continue;
		if(fabs(iter->LimitPrice-price)<error_amount){
			for(iterCanceling=vCancelingOrders.begin();iterCanceling!=vCancelingOrders.end();iterCanceling++){
				if(strcmp(iterCanceling->InstrumentID,iter->InstrumentID)==0 && iterCanceling->FrontID==iter->FrontID && iterCanceling->SessionID==iter->SessionID && strcmp(iterCanceling->OrderRef,iter->OrderRef)==0)
					break;
			}
			if(iterCanceling!=vCancelingOrders.end())	// 不重复发送撤单指令
				continue;

			CThostFtdcInputOrderActionField Req{};

			memset(&Req,0x00,sizeof(Req));
			strncpy(Req.BrokerID,iter->BrokerID,sizeof(Req.BrokerID)-1);
			strncpy(Req.UserID, iter->InvestorID, sizeof(Req.UserID) - 1);
			strncpy(Req.InvestorID,iter->InvestorID,sizeof(Req.InvestorID)-1);
			strcpy(Req.ExchangeID,iter->ExchangeID);
			strcpy(Req.InstrumentID,iter->InstrumentID);
			strcpy(Req.OrderRef,iter->OrderRef);
			Req.OrderActionRef=pTradeRsp->TradeOrderRef++;
			Req.FrontID=iter->FrontID;
			Req.SessionID=iter->SessionID;
			strcpy(Req.OrderSysID,iter->OrderSysID);
			Req.ActionFlag=THOST_FTDC_AF_Delete;
			
			if(pTradeRsp->m_pTradeReq->ReqOrderAction(&Req,pTradeRsp->m_nTradeRequestID++)<0)
				break;
			vCancelingOrders.push_back(Req);
		}
	}
}
void order_cancel_all_orders()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;

	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;

	std::vector<CThostFtdcOrderField>::iterator iter;
	std::vector<CThostFtdcInputOrderActionField>::iterator iterCanceling;
	for(iter=vOrders.begin();iter!=vOrders.end();iter++){
		if(strcmp(iter->InvestorID,order_curr_accname)!=0 || strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)!=0 || iter->OrderStatus==THOST_FTDC_OST_AllTraded || iter->OrderStatus==THOST_FTDC_OST_Canceled)
			continue;
		for(iterCanceling=vCancelingOrders.begin();iterCanceling!=vCancelingOrders.end();iterCanceling++){
			if(strcmp(iterCanceling->InstrumentID,iter->InstrumentID)==0 && iterCanceling->FrontID==iter->FrontID && iterCanceling->SessionID==iter->SessionID && strcmp(iterCanceling->OrderRef,iter->OrderRef)==0)
				break;
		}
		if(iterCanceling!=vCancelingOrders.end())	// 不重复发送撤单指令
			continue;
		CThostFtdcInputOrderActionField Req{};

		memset(&Req,0x00,sizeof(Req));
		strncpy(Req.BrokerID,iter->BrokerID,sizeof(Req.BrokerID)-1);
		strncpy(Req.UserID, iter->InvestorID, sizeof(Req.UserID) - 1);
		strncpy(Req.InvestorID,iter->InvestorID,sizeof(Req.InvestorID)-1);
		strcpy(Req.ExchangeID,iter->ExchangeID);
		strcpy(Req.InstrumentID,iter->InstrumentID);
		strcpy(Req.OrderRef,iter->OrderRef);
		Req.OrderActionRef=pTradeRsp->TradeOrderRef++;
		Req.FrontID=iter->FrontID;
		Req.SessionID=iter->SessionID;
		strcpy(Req.OrderSysID,iter->OrderSysID);
		Req.ActionFlag=THOST_FTDC_AF_Delete;
		
		if(pTradeRsp->m_pTradeReq->ReqOrderAction(&Req,pTradeRsp->m_nTradeRequestID++)<0)
			break;
		vCancelingOrders.push_back(Req);
	}
	order_redraw();
}

char getOrderOffsetFlag(const char* szInstrument,char cDirection,unsigned int nQty,unsigned int &nOpen,unsigned int &nClose,unsigned int &nCloseToday)
{
	nOpen=0;
	nClose=0;
	nCloseToday=0;

	std::vector<stPosition_t>::iterator iter;
	for(iter=vPositions.begin();iter!=vPositions.end();iter++){
		if(strcmp(iter->InstrumentID,szInstrument)==0){
			if(cDirection==THOST_FTDC_D_Buy){
				if(iter->SellVolume-iter->FrozenSellVolume>0){
					// 只有上期所及能源中心才需要平今
					if((strcmp(iter->ExchangeID,"SHFE")==0 || strcmp(iter->ExchangeID, "INE") == 0 ) && iter->TodaySellVolume-iter->TodayFrozenSellVolume>0){
						nCloseToday=nQty>iter->TodaySellVolume-iter->TodayFrozenSellVolume?iter->TodaySellVolume-iter->TodayFrozenSellVolume:nQty;
						nClose=nQty>iter->SellVolume-iter->FrozenSellVolume?iter->SellVolume-iter->FrozenSellVolume-nCloseToday:nQty-nCloseToday;
					}else{
						nClose=nQty>iter->SellVolume-iter->FrozenSellVolume?iter->SellVolume-iter->FrozenSellVolume-nCloseToday:nQty-nCloseToday;
					}
				}
				if(nQty>iter->SellVolume-iter->FrozenSellVolume){
					// 如果还有剩余则开仓
					nOpen=nQty-(iter->SellVolume-iter->FrozenSellVolume);
				}
			}else{
				if(iter->BuyVolume-iter->FrozenBuyVolume>0){
					// 只有上期所及能源中心才需要平今
					if((strcmp(iter->ExchangeID, "SHFE") == 0 || strcmp(iter->ExchangeID, "INE") == 0) && iter->TodayBuyVolume-iter->TodayFrozenBuyVolume>0){
						nCloseToday=nQty>iter->TodayBuyVolume-iter->TodayFrozenBuyVolume?iter->TodayBuyVolume-iter->TodayFrozenBuyVolume:nQty;
						nClose=nQty>iter->BuyVolume-iter->FrozenBuyVolume?iter->BuyVolume-iter->FrozenBuyVolume-nCloseToday:nQty-nCloseToday;
					}else{
						nClose=nQty>iter->BuyVolume-iter->FrozenBuyVolume?iter->BuyVolume-iter->FrozenBuyVolume-nCloseToday:nQty-nCloseToday;
					}
				}
				if(nQty>iter->BuyVolume-iter->FrozenBuyVolume){
					// 如果还有剩余则开仓
					nOpen=nQty-(iter->BuyVolume-iter->FrozenBuyVolume);
				}
			}
			return 0;
		}
	}
	nOpen=nQty;

	return 0;
}

void order_move_forward_1_line()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	if(order_curr_price==DBL_MAX){
		order_curr_price=high_limit;
	}else if(order_curr_price>=high_limit+error_amount || order_curr_price<=low_limit-error_amount){
		order_curr_price=high_limit;
	}else{
		if(order_curr_price>=low_limit+error_amount)
			order_curr_price-=PriceTick;
		if(order_curr_price+PriceTick*(order_max_lines-1)<=order_page_top_price-error_amount)
			order_page_top_price=order_curr_price+PriceTick*(order_max_lines-1);
	}

	order_redraw();
}

void order_move_backward_1_line()
{
	if(order_symbol_index<0)
		return;
	
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double error_amount=1.0/pow(10.0,vquotes[order_symbol_index].precision)/2.0;
	
	if(high_limit==DBL_MAX || low_limit==DBL_MAX)
		return;
	if(order_page_top_price==DBL_MAX){
		order_page_top_price=high_limit;
	}else if(order_page_top_price>=high_limit+error_amount || order_page_top_price<=low_limit-error_amount){
		order_page_top_price=high_limit;
	}
	if(order_curr_price==DBL_MAX){
		order_curr_price=high_limit;
	}else if(order_curr_price>=high_limit+error_amount || order_curr_price<=low_limit-error_amount){
		order_curr_price=high_limit;
	}else{
		order_curr_price+=PriceTick;
		if(order_curr_price>=order_page_top_price+error_amount)
			order_page_top_price=order_curr_price;
	}
	order_redraw();
}


// Order List

void orderlist_refresh_screen()
{
	int y,x;
	
	if(working_window!=WIN_ORDERLIST)
		return;
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
	orderlist_max_lines=y-2;
	orderlist_redraw();
}

void orderlist_redraw()
{
	erase();
	orderlist_display_title();
	orderlist_display_orders();
	orderlist_display_status();
	orderlist_display_focus();
}

void orderlist_reset(const char *user)
{
	// Order List Curses
	orderlist_curr_line=0,orderlist_curr_col=1,orderlist_max_cols=7;
	orderlist_curr_pos=0,orderlist_curr_col_pos=2;
	std::vector<CThostFtdcOrderField>::iterator iter;
	for(iter=vOrders.begin();iter!=vOrders.end();){
		if(strcmp(iter->InvestorID,user)==0){
			vOrders.erase(iter);
			iter=vOrders.begin();
			continue;
		}else{
			iter++;
		}
	}
	if(working_window==WIN_ORDERLIST)
		orderlist_redraw();
}

void orderlist_display_title()
{
	int y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_ORDERLIST)
		return;
	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	
	for(auto iter=vorderlist_columns.begin();iter!=vorderlist_columns.end();iter++,pos++){
		if(morderlist_columns[*iter]==false)
			continue;
		if(*iter!=ORDERLIST_COL_ACC_ID && *iter!=ORDERLIST_COL_SYMBOL && *iter!=ORDERLIST_COL_SYMBOL_NAME && pos<orderlist_curr_col_pos)
			continue;
		if(maxx-x<orderlist_column_items[*iter].width)
			break;
		switch(*iter){
		case ORDERLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_ACC_ID].width,orderlist_column_items[ORDERLIST_COL_ACC_ID].name);
			x+=orderlist_column_items[ORDERLIST_COL_ACC_ID].width;
			break;
		case ORDERLIST_COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SYMBOL].width,orderlist_column_items[ORDERLIST_COL_SYMBOL].name);
			x+=orderlist_column_items[ORDERLIST_COL_SYMBOL].width;
			break;
		case ORDERLIST_COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SYMBOL_NAME].width,orderlist_column_items[ORDERLIST_COL_SYMBOL_NAME].name);
			x+=orderlist_column_items[ORDERLIST_COL_SYMBOL_NAME].width+1;
			break;
		case ORDERLIST_COL_DIRECTION:		//direction
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width+2,orderlist_column_items[ORDERLIST_COL_DIRECTION].name);
			x+=orderlist_column_items[ORDERLIST_COL_DIRECTION].width+1;
			break;
		case ORDERLIST_COL_VOLUME:		//close
			mvprintw(y,x,"%*s",orderlist_column_items[ORDERLIST_COL_VOLUME].width+2,orderlist_column_items[ORDERLIST_COL_VOLUME].name);
			x+=orderlist_column_items[ORDERLIST_COL_VOLUME].width+1;
			break;
		case ORDERLIST_COL_VOLUME_FILLED:		//close
			mvprintw(y,x,"%*s",orderlist_column_items[ORDERLIST_COL_VOLUME_FILLED].width+2,orderlist_column_items[ORDERLIST_COL_VOLUME_FILLED].name);
			x+=orderlist_column_items[ORDERLIST_COL_VOLUME_FILLED].width+1;
			break;
		case ORDERLIST_COL_PRICE:		//volume
			mvprintw(y,x,"%*s",orderlist_column_items[ORDERLIST_COL_PRICE].width+2,orderlist_column_items[ORDERLIST_COL_PRICE].name);
			x+=orderlist_column_items[ORDERLIST_COL_PRICE].width+1;
			break;
		case ORDERLIST_COL_AVG_PRICE:		//close
			mvprintw(y,x,"%*s",orderlist_column_items[ORDERLIST_COL_AVG_PRICE].width+4,orderlist_column_items[ORDERLIST_COL_AVG_PRICE].name);
			x+=orderlist_column_items[ORDERLIST_COL_AVG_PRICE].width+1;
			break;
		case ORDERLIST_COL_APPLY_TIME:		//volume
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_APPLY_TIME].width,orderlist_column_items[ORDERLIST_COL_APPLY_TIME].name);
			x+=orderlist_column_items[ORDERLIST_COL_APPLY_TIME].width+1;
			break;
		case ORDERLIST_COL_UPDATE_TIME:		//close
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_UPDATE_TIME].width,orderlist_column_items[ORDERLIST_COL_UPDATE_TIME].name);
			x+=orderlist_column_items[ORDERLIST_COL_UPDATE_TIME].width+1;
			break;
		case ORDERLIST_COL_STATUS:		//volume
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,orderlist_column_items[ORDERLIST_COL_STATUS].name);
			x+=orderlist_column_items[ORDERLIST_COL_STATUS].width+1;
			break;
		case ORDERLIST_COL_SH_FLAG:		//close
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SH_FLAG].width,orderlist_column_items[ORDERLIST_COL_SH_FLAG].name);
			x+=orderlist_column_items[ORDERLIST_COL_SH_FLAG].width+1;
			break;
		case ORDERLIST_COL_ORDERID:		//close
			mvprintw(y,x,"%*s",orderlist_column_items[ORDERLIST_COL_ORDERID].width+3,orderlist_column_items[ORDERLIST_COL_ORDERID].name);
			x+=orderlist_column_items[ORDERLIST_COL_ORDERID].width+1;
			break;
		case ORDERLIST_COL_EXCHANGE_NAME:		//volume
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_EXCHANGE_NAME].width,orderlist_column_items[ORDERLIST_COL_EXCHANGE_NAME].name);
			x+=orderlist_column_items[ORDERLIST_COL_EXCHANGE_NAME].width+1;
			break;
		case ORDERLIST_COL_DESC:		//volume
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DESC].width,orderlist_column_items[ORDERLIST_COL_DESC].name);
			x+=orderlist_column_items[ORDERLIST_COL_DESC].width+1;
			break;
		default:
			break;
		}
	}
}

void orderlist_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_ORDERLIST)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%ld]",orderlist_curr_pos+orderlist_curr_line,vOrders.size());
	mvprintw(y - 1, 15, "%s", status_message);
	mvprintw(y-1,x-25,"%s %s",pTradeRsp->user,tradetime);
}



void orderlist_display_order(int index)
{
	size_t i,y,x,pos=0,maxy,maxx,j;

	if(working_window!=WIN_ORDERLIST)
		return;
	getmaxyx(stdscr,maxy,maxx);
	i=index;
	if(i<orderlist_curr_pos || i>orderlist_curr_pos+orderlist_max_lines-1)
		return;
	y=i-orderlist_curr_pos+1;
	x=0;

	for(j=0;j<vquotes.size();j++)
		if(strcmp(vquotes[j].product_id,vOrders[i].InstrumentID)==0)
			break;
	if(j==vquotes.size())
		return;
	move(y,0);
	clrtoeol();
	
	for(auto iter=vorderlist_columns.begin();iter!=vorderlist_columns.end();iter++,pos++){
		if(morderlist_columns[*iter]==false)
			continue;
		if(*iter!=ORDERLIST_COL_ACC_ID && *iter!=ORDERLIST_COL_SYMBOL && *iter!=ORDERLIST_COL_SYMBOL_NAME && pos<orderlist_curr_col_pos)
			continue;
		if(maxx-x<orderlist_column_items[*iter].width)
			break;
		switch(*iter){
		case ORDERLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_ACC_ID].width,vOrders[i].InvestorID);
			x+=orderlist_column_items[ORDERLIST_COL_ACC_ID].width;
			break;
		case ORDERLIST_COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SYMBOL].width,vOrders[i].InstrumentID);
			x+=orderlist_column_items[ORDERLIST_COL_SYMBOL].width;
			break;
		case ORDERLIST_COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SYMBOL].width, STR(vquotes[j].product_name).c_str());
			x+=orderlist_column_items[ORDERLIST_COL_SYMBOL_NAME].width+1;
			break;
		case ORDERLIST_COL_DIRECTION:		//close
			if(vOrders[i].Direction==THOST_FTDC_D_Buy && vOrders[i].CombOffsetFlag[0]==THOST_FTDC_OF_Open)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width,"买开");
			else if(vOrders[i].Direction==THOST_FTDC_D_Buy && vOrders[i].CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width,"买平今");
			else if(vOrders[i].Direction==THOST_FTDC_D_Sell && vOrders[i].CombOffsetFlag[0]==THOST_FTDC_OF_Open)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width," 卖开");
			else if(vOrders[i].Direction==THOST_FTDC_D_Sell && vOrders[i].CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width," 卖平今");
			else if(vOrders[i].Direction==THOST_FTDC_D_Buy)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width,"买平");
			else
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DIRECTION].width," 卖平");
			x+=orderlist_column_items[ORDERLIST_COL_DIRECTION].width+1;
			break;
		case ORDERLIST_COL_VOLUME:		//volume
			mvprintw(y,x,"%*d",orderlist_column_items[ORDERLIST_COL_VOLUME].width,vOrders[i].VolumeTotalOriginal);
			x+=orderlist_column_items[ORDERLIST_COL_VOLUME].width+1;
			break;
		case ORDERLIST_COL_VOLUME_FILLED:		//volume
			mvprintw(y,x,"%*d",orderlist_column_items[ORDERLIST_COL_VOLUME_FILLED].width,vOrders[i].VolumeTraded);
			x+=orderlist_column_items[ORDERLIST_COL_VOLUME_FILLED].width+1;
			break;
		case ORDERLIST_COL_PRICE:		//close
			if(vOrders[i].LimitPrice==DBL_MAX || vOrders[i].LimitPrice==0)
				mvprintw(y,x,"%*c",orderlist_column_items[ORDERLIST_COL_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",orderlist_column_items[ORDERLIST_COL_PRICE].width,vquotes[j].precision,vOrders[i].LimitPrice);
			x+=orderlist_column_items[ORDERLIST_COL_PRICE].width+1;
			break;
		case ORDERLIST_COL_AVG_PRICE:		//close
			if(vOrders[i].LimitPrice==DBL_MAX || vOrders[i].LimitPrice==0)
				mvprintw(y,x,"%*c",orderlist_column_items[ORDERLIST_COL_AVG_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",orderlist_column_items[ORDERLIST_COL_AVG_PRICE].width,vquotes[j].precision,vOrders[i].LimitPrice);
			x+=orderlist_column_items[ORDERLIST_COL_AVG_PRICE].width+1;
			break;
		case ORDERLIST_COL_APPLY_TIME:		//product_name
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_APPLY_TIME].width,vOrders[i].InsertTime);
			x+=orderlist_column_items[ORDERLIST_COL_APPLY_TIME].width+1;
			break;
		case ORDERLIST_COL_UPDATE_TIME:		//product_name
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_UPDATE_TIME].width,vOrders[i].UpdateTime);
			x+=orderlist_column_items[ORDERLIST_COL_UPDATE_TIME].width+1;
			break;
		case ORDERLIST_COL_STATUS:		//close
			if(vOrders[i].OrderStatus==THOST_FTDC_OST_AllTraded)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,"全部成交");
			else if(vOrders[i].OrderStatus==THOST_FTDC_OST_Canceled)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,"已撤消");
			else if(vOrders[i].OrderStatus==THOST_FTDC_OST_Unknown)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,"申报中");
			else if(vOrders[i].OrderStatus==THOST_FTDC_OST_NoTradeQueueing)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,"已报入");
			else if(vOrders[i].OrderStatus==THOST_FTDC_OST_PartTradedQueueing)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,"部分成交");
			else
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_STATUS].width,"未知");
			x+=orderlist_column_items[ORDERLIST_COL_STATUS].width+1;
			break;
		case ORDERLIST_COL_SH_FLAG:		//close
			if(vOrders[i].CombHedgeFlag[0]==THOST_FTDC_HF_Speculation)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SH_FLAG].width,"投");
			else if(vOrders[i].CombHedgeFlag[0]==THOST_FTDC_HF_Hedge)
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SH_FLAG].width," 保");
			else
				mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_SH_FLAG].width,"套利");
			x+=orderlist_column_items[ORDERLIST_COL_SH_FLAG].width+1;
			break;
		case ORDERLIST_COL_ORDERID:		//product_name
			mvprintw(y,x,"%*s",orderlist_column_items[ORDERLIST_COL_ORDERID].width,vOrders[i].OrderSysID);
			x+=orderlist_column_items[ORDERLIST_COL_ORDERID].width+1;
			break;
		case ORDERLIST_COL_EXCHANGE_NAME:		//product_name
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_EXCHANGE_NAME].width,vOrders[i].ExchangeID);
			x+=orderlist_column_items[ORDERLIST_COL_EXCHANGE_NAME].width+1;
			break;
		case ORDERLIST_COL_DESC:		//product_name
			mvprintw(y,x,"%-*s",orderlist_column_items[ORDERLIST_COL_DESC].width,STR(vOrders[i].StatusMsg).c_str());
			x+=orderlist_column_items[ORDERLIST_COL_DESC].width+1;
			break;
		default:
			break;
		}
	}
}

void orderlist_display_orders()
{
	for(size_t i=orderlist_curr_pos;i<=orderlist_curr_pos+orderlist_max_lines-1 && i<vOrders.size();i++)
		orderlist_display_order(i);
}

void orderlist_display_focus()
{
	if(orderlist_curr_line!=0)
		mvchgat(orderlist_curr_line,0,-1,A_REVERSE,0,NULL);
}

void orderlist_scroll_left_1_column()
{
	if(orderlist_curr_col_pos==2)
		return;
	while(morderlist_columns[vorderlist_columns[--orderlist_curr_col_pos]]==false); //	取消所在列的反白显示
	orderlist_redraw();
}
void orderlist_scroll_right_1_column()
{
	if(orderlist_curr_col_pos==sizeof(orderlist_column_items)/sizeof(column_item_t)-1)
		return;
	while(morderlist_columns[vorderlist_columns[++orderlist_curr_col_pos]]==false); //	取消所在列的反白显示
	orderlist_redraw();
}

void orderlist_move_forward_1_line()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
		orderlist_redraw();
		return;
	}
	if(orderlist_curr_line==vOrders.size()-orderlist_curr_pos)	// Already bottom
		return;
	if(orderlist_curr_line!=orderlist_max_lines){
		orderlist_curr_line++;
	}else{
		orderlist_curr_pos++;
	}
	orderlist_redraw();
}

void orderlist_move_backward_1_line()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
		orderlist_redraw();
		return;
	}
	if(orderlist_curr_line==1 && orderlist_curr_pos==0)	// Already top
		return;
	if(orderlist_curr_line>1){
		orderlist_curr_line--;
	}else{
		orderlist_curr_pos--;
	}
	orderlist_redraw();
}

void orderlist_scroll_forward_1_line()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}
	if(orderlist_curr_pos==vOrders.size()-1){	//Already bottom
		return;
	}	
	if(orderlist_curr_line!=1)
		orderlist_curr_line--;
	
	orderlist_curr_pos++;
	orderlist_redraw();
}

void orderlist_scroll_backward_1_line()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}
	if(orderlist_curr_pos==0){	//Already top
		return;
	}
	
	if(orderlist_curr_line!=orderlist_max_lines)
		orderlist_curr_line++;
	
	orderlist_curr_pos--;
	orderlist_redraw();
}

void orderlist_goto_file_top()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}
	if(orderlist_curr_line==1 && orderlist_curr_pos==0)	// Already top
		return;
	if(orderlist_curr_pos==0){
		orderlist_curr_line=1;
	}else{
		orderlist_curr_pos=0;
		orderlist_curr_line=1;
	}
	orderlist_redraw();
}

void orderlist_goto_file_bottom()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}
	if(orderlist_curr_line==vOrders.size()-orderlist_curr_pos)	// Already bottom
		return;
	if(vOrders.size()-orderlist_curr_pos<=orderlist_max_lines){
		orderlist_curr_line=vOrders.size()-orderlist_curr_pos;
	}else{
		orderlist_curr_pos=vOrders.size()-orderlist_max_lines;
		orderlist_curr_line=orderlist_max_lines;
	}
	orderlist_redraw();
}

void orderlist_goto_page_top()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}else{
		orderlist_curr_line=1;
	}
	orderlist_redraw();
}
void orderlist_goto_page_bottom()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}
	if(orderlist_curr_line==vOrders.size()-orderlist_curr_pos)	// Already bottom
		return;
	if(vOrders.size()-orderlist_curr_pos<orderlist_max_lines){
		orderlist_curr_line=vOrders.size()-orderlist_curr_pos;
	}else{
		orderlist_curr_line=orderlist_max_lines;
	}
	orderlist_redraw();
}
void orderlist_goto_page_middle()
{
	if(vOrders.empty())
		return;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
	}
	if(vOrders.size()-orderlist_curr_pos==1)	// Only 1 line
		return;
	if(vOrders.size()-orderlist_curr_pos<orderlist_max_lines){
		orderlist_curr_line=(vOrders.size()-orderlist_curr_pos)/2+1;
	}else{
		orderlist_curr_line=orderlist_max_lines/2+1;
	}
	orderlist_redraw();
}

void orderlist_move_forward_1_page()
{
	int i;
	for(i=0;i<orderlist_max_lines;i++)
		orderlist_scroll_forward_1_line();
}
void orderlist_move_backward_1_page()
{
	int i;
	for(i=0;i<orderlist_max_lines;i++)
		orderlist_scroll_backward_1_line();
}
void orderlist_move_forward_half_page()
{
	int i;
	for(i=0;i<orderlist_max_lines/2;i++)
		orderlist_scroll_forward_1_line();
}
void orderlist_move_backward_half_page()
{
	int i;
	for(i=0;i<orderlist_max_lines/2;i++)
		orderlist_scroll_backward_1_line();
}


// Fill List

void filllist_refresh_screen()
{
	int y,x;
	
	if(working_window!=WIN_FILLLIST)
		return;
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
	filllist_max_lines=y-2;
	filllist_redraw();
}

void filllist_redraw()
{
	erase();
	filllist_display_title();
	filllist_display_filledorders();
	filllist_display_status();
	filllist_display_focus();
}

void filllist_reset(const char *user)
{
	// Filled Order List Curses
	filllist_curr_line=0,filllist_curr_col=1,filllist_max_cols=7;
	filllist_curr_pos=0,filllist_curr_col_pos=2;
	std::vector<CThostFtdcTradeField>::iterator iter;
	for(iter=vFilledOrders.begin();iter!=vFilledOrders.end();){
		if(strcmp(iter->InvestorID,user)==0){
			vFilledOrders.erase(iter);
			iter=vFilledOrders.begin();
			continue;
		}else{
			iter++;
		}
	}
	if(working_window==WIN_FILLLIST)
		filllist_redraw();
}
void filllist_display_title()
{
	int y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_FILLLIST)
		return;
	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	
	for(auto iter=vfilllist_columns.begin();iter!=vfilllist_columns.end();iter++,pos++){
		if(mfilllist_columns[*iter]==false)
			continue;
		if(*iter!=FILLLIST_COL_ACC_ID && *iter!=FILLLIST_COL_SYMBOL && *iter!=FILLLIST_COL_SYMBOL_NAME && pos<filllist_curr_col_pos)
			continue;
		if(maxx-x<filllist_column_items[*iter].width)
			break;
		switch(*iter){
		case FILLLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_ACC_ID].width,filllist_column_items[FILLLIST_COL_ACC_ID].name);
			x+=filllist_column_items[FILLLIST_COL_ACC_ID].width;
			break;
		case FILLLIST_COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SYMBOL].width,filllist_column_items[FILLLIST_COL_SYMBOL].name);
			x+=filllist_column_items[FILLLIST_COL_SYMBOL].width;
			break;
		case FILLLIST_COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SYMBOL_NAME].width,filllist_column_items[FILLLIST_COL_SYMBOL_NAME].name);
			x+=filllist_column_items[FILLLIST_COL_SYMBOL_NAME].width+1;
			break;
		case FILLLIST_COL_DIRECTION:		//direction
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width,filllist_column_items[FILLLIST_COL_DIRECTION].name);
			x+=filllist_column_items[FILLLIST_COL_DIRECTION].width+1;
			break;
		case FILLLIST_COL_VOLUME:		//close
			mvprintw(y,x,"%*s",filllist_column_items[FILLLIST_COL_VOLUME].width+2,filllist_column_items[FILLLIST_COL_VOLUME].name);
			x+=filllist_column_items[FILLLIST_COL_VOLUME].width+1;
			break;
		case FILLLIST_COL_PRICE:		//volume
			mvprintw(y,x,"%*s",filllist_column_items[FILLLIST_COL_PRICE].width+2,filllist_column_items[FILLLIST_COL_PRICE].name);
			x+=filllist_column_items[FILLLIST_COL_PRICE].width+1;
			break;
		case FILLLIST_COL_TIME:		//volume
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_TIME].width,filllist_column_items[FILLLIST_COL_TIME].name);
			x+=filllist_column_items[FILLLIST_COL_TIME].width+1;
			break;
		case FILLLIST_COL_SH_FLAG:		//close
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SH_FLAG].width,filllist_column_items[FILLLIST_COL_SH_FLAG].name);
			x+=filllist_column_items[FILLLIST_COL_SH_FLAG].width+1;
			break;
		case FILLLIST_COL_FILLID:		//close
			mvprintw(y,x,"%*s",filllist_column_items[FILLLIST_COL_FILLID].width+3,filllist_column_items[FILLLIST_COL_FILLID].name);
			x+=filllist_column_items[FILLLIST_COL_FILLID].width+1;
			break;
		case FILLLIST_COL_ORDERID:		//close
			mvprintw(y,x,"%*s",filllist_column_items[FILLLIST_COL_ORDERID].width+3,filllist_column_items[FILLLIST_COL_ORDERID].name);
			x+=filllist_column_items[FILLLIST_COL_ORDERID].width+1;
			break;
		case FILLLIST_COL_EXCHANGE_NAME:		//volume
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_EXCHANGE_NAME].width,filllist_column_items[FILLLIST_COL_EXCHANGE_NAME].name);
			x+=filllist_column_items[FILLLIST_COL_EXCHANGE_NAME].width+1;
			break;
		default:
			break;
		}
	}
}

void filllist_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_FILLLIST)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%ld]",filllist_curr_pos+filllist_curr_line,vFilledOrders.size());
	mvprintw(y - 1, 15, "%s", status_message);
	mvprintw(y-1,x-25,"%s %s", pTradeRsp->user,tradetime);
}



void filllist_display_filledorder(int index)
{
	size_t i,y,x,pos=0,maxy,maxx,j;

	if(working_window!=WIN_FILLLIST)
		return;
	getmaxyx(stdscr,maxy,maxx);
	i=index;
	if(i<filllist_curr_pos || i>filllist_curr_pos+filllist_max_lines-1)
		return;
	y=i-filllist_curr_pos+1;
	x=0;

	for(j=0;j<vquotes.size();j++)
		if(strcmp(vquotes[j].product_id,vFilledOrders[i].InstrumentID)==0)
			break;
	if(j==vquotes.size())
		return;
	move(y,0);
	clrtoeol();
	
	for(auto iter=vfilllist_columns.begin();iter!=vfilllist_columns.end();iter++,pos++){
		if(mfilllist_columns[*iter]==false)
			continue;
		if(*iter!=FILLLIST_COL_ACC_ID && *iter!=FILLLIST_COL_SYMBOL && *iter!=FILLLIST_COL_SYMBOL_NAME && pos<filllist_curr_col_pos)
			continue;
		if(maxx-x<filllist_column_items[*iter].width)
			break;
		switch(*iter){
		case FILLLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_ACC_ID].width,vFilledOrders[i].InvestorID);
			x+=filllist_column_items[FILLLIST_COL_ACC_ID].width;
			break;
		case FILLLIST_COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SYMBOL].width,vFilledOrders[i].InstrumentID);
			x+=filllist_column_items[FILLLIST_COL_SYMBOL].width;
			break;
		case FILLLIST_COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SYMBOL].width, STR(vquotes[j].product_name).c_str());
			x+=filllist_column_items[FILLLIST_COL_SYMBOL_NAME].width+1;
			break;
		case FILLLIST_COL_DIRECTION:		//close
			if(vFilledOrders[i].Direction==THOST_FTDC_D_Buy && vFilledOrders[i].OffsetFlag==THOST_FTDC_OF_Open)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width,"买开");
			else if(vFilledOrders[i].Direction==THOST_FTDC_D_Buy && vFilledOrders[i].OffsetFlag==THOST_FTDC_OF_CloseToday)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width,"买平今");
			else if(vFilledOrders[i].Direction==THOST_FTDC_D_Sell && vFilledOrders[i].OffsetFlag==THOST_FTDC_OF_Open)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width," 卖开");
			else if(vFilledOrders[i].Direction==THOST_FTDC_D_Sell && vFilledOrders[i].OffsetFlag==THOST_FTDC_OF_CloseToday)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width," 卖平今");
			else if(vFilledOrders[i].Direction==THOST_FTDC_D_Buy)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width,"买平");
			else
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_DIRECTION].width," 卖平");
			x+=filllist_column_items[FILLLIST_COL_DIRECTION].width+1;
			break;
		case FILLLIST_COL_VOLUME:		//volume
			mvprintw(y,x,"%*d",filllist_column_items[FILLLIST_COL_VOLUME].width,vFilledOrders[i].Volume);
			x+=filllist_column_items[FILLLIST_COL_VOLUME].width+1;
			break;
		case FILLLIST_COL_PRICE:		//close
			if(vFilledOrders[i].Price==DBL_MAX || vFilledOrders[i].Price==0)
				mvprintw(y,x,"%*c",filllist_column_items[FILLLIST_COL_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",filllist_column_items[FILLLIST_COL_PRICE].width,vquotes[j].precision,vFilledOrders[i].Price);
			x+=filllist_column_items[FILLLIST_COL_PRICE].width+1;
			break;
		case FILLLIST_COL_TIME:		//product_name
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_TIME].width,vFilledOrders[i].TradeTime);
			x+=filllist_column_items[FILLLIST_COL_TIME].width+1;
			break;
		case FILLLIST_COL_SH_FLAG:		//close
			if(vFilledOrders[i].HedgeFlag==THOST_FTDC_HF_Speculation)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SH_FLAG].width,"投");
			else if(vFilledOrders[i].HedgeFlag==THOST_FTDC_HF_Hedge)
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SH_FLAG].width," 保");
			else
				mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_SH_FLAG].width,"套利");
			x+=filllist_column_items[FILLLIST_COL_SH_FLAG].width+1;
			break;
		case FILLLIST_COL_FILLID:		//product_name
			mvprintw(y,x,"%*s",filllist_column_items[FILLLIST_COL_FILLID].width,vFilledOrders[i].TradeID);
			x+=filllist_column_items[FILLLIST_COL_FILLID].width+1;
			break;
		case FILLLIST_COL_ORDERID:		//product_name
			mvprintw(y,x,"%*s",filllist_column_items[FILLLIST_COL_ORDERID].width,vFilledOrders[i].OrderSysID);
			x+=filllist_column_items[FILLLIST_COL_ORDERID].width+1;
			break;
		case FILLLIST_COL_EXCHANGE_NAME:		//product_name
			mvprintw(y,x,"%-*s",filllist_column_items[FILLLIST_COL_EXCHANGE_NAME].width,vFilledOrders[i].ExchangeID);
			x+=filllist_column_items[FILLLIST_COL_EXCHANGE_NAME].width+1;
			break;
		default:
			break;
		}
	}
}

void filllist_display_filledorders()
{
	for(size_t i=filllist_curr_pos;i<=filllist_curr_pos+filllist_max_lines-1 && i<vFilledOrders.size();i++)
		filllist_display_filledorder(i);
}

void filllist_display_focus()
{
	if(filllist_curr_line!=0)
		mvchgat(filllist_curr_line,0,-1,A_REVERSE,0,NULL);
}

void filllist_scroll_left_1_column()
{
	if(filllist_curr_col_pos==2)
		return;
	while(mfilllist_columns[vfilllist_columns[--filllist_curr_col_pos]]==false); //	取消所在列的反白显示
	filllist_redraw();
}
void filllist_scroll_right_1_column()
{
	if(filllist_curr_col_pos==sizeof(filllist_column_items)/sizeof(column_item_t)-1)
		return;
	while(mfilllist_columns[vfilllist_columns[++filllist_curr_col_pos]]==false); //	取消所在列的反白显示
	filllist_redraw();
}

void filllist_move_forward_1_line()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
		filllist_redraw();
		return;
	}
	if(filllist_curr_line==vFilledOrders.size()-filllist_curr_pos)	// Already bottom
		return;
	if(filllist_curr_line!=filllist_max_lines){
		filllist_curr_line++;
	}else{
		filllist_curr_pos++;
	}
	filllist_redraw();
}

void filllist_move_backward_1_line()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
		filllist_redraw();
		return;
	}
	if(filllist_curr_line==1 && filllist_curr_pos==0)	// Already top
		return;
	if(filllist_curr_line>1){
		filllist_curr_line--;
	}else{
		filllist_curr_pos--;
	}
	filllist_redraw();
}

void filllist_scroll_forward_1_line()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}
	if(filllist_curr_pos==vFilledOrders.size()-1){	//Already bottom
		return;
	}	
	if(filllist_curr_line!=1)
		filllist_curr_line--;
	
	filllist_curr_pos++;
	filllist_redraw();
}

void filllist_scroll_backward_1_line()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}
	if(filllist_curr_pos==0){	//Already top
		return;
	}
	
	if(filllist_curr_line!=filllist_max_lines)
		filllist_curr_line++;
	
	filllist_curr_pos--;
	filllist_redraw();
}

void filllist_goto_file_top()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}
	if(filllist_curr_line==1 && filllist_curr_pos==0)	// Already top
		return;
	if(filllist_curr_pos==0){
		filllist_curr_line=1;
	}else{
		filllist_curr_pos=0;
		filllist_curr_line=1;
	}
	filllist_redraw();
}

void filllist_goto_file_bottom()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}
	if(filllist_curr_line==vFilledOrders.size()-filllist_curr_pos)	// Already bottom
		return;
	if(vFilledOrders.size()-filllist_curr_pos<=filllist_max_lines){
		filllist_curr_line=vFilledOrders.size()-filllist_curr_pos;
	}else{
		filllist_curr_pos=vFilledOrders.size()-filllist_max_lines;
		filllist_curr_line=filllist_max_lines;
	}
	filllist_redraw();
}

void filllist_goto_page_top()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}else{
		filllist_curr_line=1;
	}
	filllist_redraw();
}
void filllist_goto_page_bottom()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}
	if(filllist_curr_line==vFilledOrders.size()-filllist_curr_pos)	// Already bottom
		return;
	if(vFilledOrders.size()-filllist_curr_pos<filllist_max_lines){
		filllist_curr_line=vFilledOrders.size()-filllist_curr_pos;
	}else{
		filllist_curr_line=filllist_max_lines;
	}
	filllist_redraw();
}
void filllist_goto_page_middle()
{
	if(vFilledOrders.empty())
		return;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
	}
	if(vFilledOrders.size()-filllist_curr_pos==1)	// Only 1 line
		return;
	if(vFilledOrders.size()-filllist_curr_pos<filllist_max_lines){
		filllist_curr_line=(vFilledOrders.size()-filllist_curr_pos)/2+1;
	}else{
		filllist_curr_line=filllist_max_lines/2+1;
	}
	filllist_redraw();
}

void filllist_move_forward_1_page()
{
	int i;
	for(i=0;i<filllist_max_lines;i++)
		filllist_scroll_forward_1_line();
}
void filllist_move_backward_1_page()
{
	int i;
	for(i=0;i<filllist_max_lines;i++)
		filllist_scroll_backward_1_line();
}
void filllist_move_forward_half_page()
{
	int i;
	for(i=0;i<filllist_max_lines/2;i++)
		filllist_scroll_forward_1_line();
}
void filllist_move_backward_half_page()
{
	int i;
	for(i=0;i<filllist_max_lines/2;i++)
		filllist_scroll_backward_1_line();
}


// Position List

void positionlist_refresh_screen()
{
	int y,x;
	
	if(working_window!=WIN_POSITION)
		return;
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
	positionlist_max_lines=y-2;
	positionlist_redraw();
}

void positionlist_redraw()
{
	erase();
	positionlist_display_title();
	positionlist_display_positions();
	positionlist_display_status();
	positionlist_display_focus();
}

void positionlist_reset(const char *user)
{
	// Position List Curses
	positionlist_curr_line=0,positionlist_curr_col=1,positionlist_max_cols=7;
	positionlist_curr_pos=0,positionlist_curr_col_pos=2;
	std::vector<stPosition_t>::iterator iter;
	for(iter=vPositions.begin();iter!=vPositions.end();){
		if(strcmp(iter->AccID,user)==0){
			vPositions.erase(iter);
			iter=vPositions.begin();
			continue;
		}else{
			iter++;
		}
	}
	if(working_window==WIN_POSITION)
		positionlist_redraw();
}
void positionlist_display_title()
{
	int y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_POSITION)
		return;
	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	
	for(auto iter=vpositionlist_columns.begin();iter!=vpositionlist_columns.end();iter++,pos++){
		if(mpositionlist_columns[*iter]==false)
			continue;
		if(*iter!=POSITIONLIST_COL_ACC_ID && *iter!=POSITIONLIST_COL_SYMBOL && *iter!=POSITIONLIST_COL_SYMBOL_NAME && pos<positionlist_curr_col_pos)
			continue;
		if(maxx-x<positionlist_column_items[*iter].width)
			break;
		switch(*iter){
		case POSITIONLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_ACC_ID].width,positionlist_column_items[POSITIONLIST_COL_ACC_ID].name);
			x+=positionlist_column_items[POSITIONLIST_COL_ACC_ID].width;
			break;
		case POSITIONLIST_COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_SYMBOL].width,positionlist_column_items[POSITIONLIST_COL_SYMBOL].name);
			x+=positionlist_column_items[POSITIONLIST_COL_SYMBOL].width;
			break;
		case POSITIONLIST_COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_SYMBOL_NAME].width,positionlist_column_items[POSITIONLIST_COL_SYMBOL_NAME].name);
			x+=positionlist_column_items[POSITIONLIST_COL_SYMBOL_NAME].width+1;
			break;
		case POSITIONLIST_COL_VOLUME:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_VOLUME].width+2,positionlist_column_items[POSITIONLIST_COL_VOLUME].name);
			x+=positionlist_column_items[POSITIONLIST_COL_VOLUME].width+1;
			break;
		case POSITIONLIST_COL_AVG_PRICE:		//volume
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_AVG_PRICE].width+2,positionlist_column_items[POSITIONLIST_COL_AVG_PRICE].name);
			x+=positionlist_column_items[POSITIONLIST_COL_AVG_PRICE].width+1;
			break;
		case POSITIONLIST_COL_PROFITLOSS:		//volume
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_PROFITLOSS].width+2,positionlist_column_items[POSITIONLIST_COL_PROFITLOSS].name);
			x+=positionlist_column_items[POSITIONLIST_COL_PROFITLOSS].width+1;
			break;
		case POSITIONLIST_COL_MARGIN:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_MARGIN].width+5,positionlist_column_items[POSITIONLIST_COL_MARGIN].name);
			x+=positionlist_column_items[POSITIONLIST_COL_MARGIN].width+1;
			break;
		case POSITIONLIST_COL_AMOUNT:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_AMOUNT].width+4,positionlist_column_items[POSITIONLIST_COL_AMOUNT].name);
			x+=positionlist_column_items[POSITIONLIST_COL_AMOUNT].width+1;
			break;
		case POSITIONLIST_COL_BUY_VOLUME:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_BUY_VOLUME].width+2,positionlist_column_items[POSITIONLIST_COL_BUY_VOLUME].name);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_VOLUME].width+1;
			break;
		case POSITIONLIST_COL_BUY_PRICE:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_BUY_PRICE].width+3,positionlist_column_items[POSITIONLIST_COL_BUY_PRICE].name);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_PRICE].width+1;
			break;
		case POSITIONLIST_COL_BUY_PROFITLOSS:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_BUY_PROFITLOSS].width+3,positionlist_column_items[POSITIONLIST_COL_BUY_PROFITLOSS].name);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_PROFITLOSS].width+1;
			break;
		case POSITIONLIST_COL_BUY_TODAY:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_BUY_TODAY].width+2,positionlist_column_items[POSITIONLIST_COL_BUY_TODAY].name);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_TODAY].width+1;
			break;
		case POSITIONLIST_COL_SELL_VOLUME:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_SELL_VOLUME].width+2,positionlist_column_items[POSITIONLIST_COL_SELL_VOLUME].name);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_VOLUME].width+1;
			break;
		case POSITIONLIST_COL_SELL_PRICE:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_SELL_PRICE].width+3,positionlist_column_items[POSITIONLIST_COL_SELL_PRICE].name);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_PRICE].width+1;
			break;
		case POSITIONLIST_COL_SELL_PROFITLOSS:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_SELL_PROFITLOSS].width+3,positionlist_column_items[POSITIONLIST_COL_SELL_PROFITLOSS].name);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_PROFITLOSS].width+1;
			break;
		case POSITIONLIST_COL_SELL_TODAY:		//close
			mvprintw(y,x,"%*s",positionlist_column_items[POSITIONLIST_COL_SELL_TODAY].width+2,positionlist_column_items[POSITIONLIST_COL_SELL_TODAY].name);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_TODAY].width+1;
			break;
		case POSITIONLIST_COL_EXCHANGE_NAME:		//volume
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_EXCHANGE_NAME].width,positionlist_column_items[POSITIONLIST_COL_EXCHANGE_NAME].name);
			x+=positionlist_column_items[POSITIONLIST_COL_EXCHANGE_NAME].width+1;
			break;
		default:
			break;
		}
	}
}

void positionlist_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_POSITION)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%ld]",positionlist_curr_pos+positionlist_curr_line,vPositions.size());
	mvprintw(y - 1, 15, "%s", status_message);
	mvprintw(y-1,x-25,"%s %s", pTradeRsp->user,tradetime);
}


void positionlist_display_position(const char *szAccID,const char *szExchangeID,const char *szInstrumentID)
{
	size_t i,y,x,pos=0,maxy,maxx,j;

	if(working_window!=WIN_POSITION)
		return;
	getmaxyx(stdscr,maxy,maxx);
	for(i=0;i<vPositions.size();i++)
		if(strcmp(vPositions[i].AccID,szAccID)==0 && strcmp(vPositions[i].ExchangeID,szExchangeID)==0 && strcmp(vPositions[i].InstrumentID,szInstrumentID)==0)
			break;
	if(i<positionlist_curr_pos || i>positionlist_curr_pos+positionlist_max_lines-1)
		return;
	if(vPositions[i].BuyVolume==0 && vPositions[i].SellVolume==0)
		return;
	y=i-positionlist_curr_pos+1;
	x=0;

	for(j=0;j<vquotes.size();j++)
		if(strcmp(vquotes[j].product_id,vPositions[i].InstrumentID)==0)
			break;
	if(j==vquotes.size())
		return;
	move(y,0);
	clrtoeol();
	
	for(auto iter=vpositionlist_columns.begin();iter!=vpositionlist_columns.end();iter++,pos++){
		if(mpositionlist_columns[*iter]==false)
			continue;
		if(*iter!=POSITIONLIST_COL_ACC_ID && *iter!=POSITIONLIST_COL_SYMBOL && *iter!=POSITIONLIST_COL_SYMBOL_NAME && pos<positionlist_curr_col_pos)
			continue;
		if(maxx-x<positionlist_column_items[*iter].width)
			break;
		switch(*iter){
		case POSITIONLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_ACC_ID].width,vPositions[i].AccID);
			x+=positionlist_column_items[POSITIONLIST_COL_ACC_ID].width;
			break;
		case POSITIONLIST_COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_SYMBOL].width,vPositions[i].InstrumentID);
			x+=positionlist_column_items[POSITIONLIST_COL_SYMBOL].width;
			break;
		case POSITIONLIST_COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_SYMBOL].width+2, STR(vquotes[j].product_name).c_str());
			x+=positionlist_column_items[POSITIONLIST_COL_SYMBOL_NAME].width+1;
			break;
		case POSITIONLIST_COL_VOLUME:		//volume
			if(vPositions[i].BuyVolume>0 && vPositions[i].SellVolume>0)
				mvprintw(y,x,"%*d*",positionlist_column_items[POSITIONLIST_COL_VOLUME].width-1,vPositions[i].Volume);
			else
				mvprintw(y,x,"%*d",positionlist_column_items[POSITIONLIST_COL_VOLUME].width,vPositions[i].Volume);
			x+=positionlist_column_items[POSITIONLIST_COL_VOLUME].width+1;
			break;
		case POSITIONLIST_COL_AVG_PRICE:		//close
			if(vPositions[i].Price==DBL_MAX || vPositions[i].Price==0)
				mvprintw(y,x,"%*c",positionlist_column_items[POSITIONLIST_COL_AVG_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",positionlist_column_items[POSITIONLIST_COL_AVG_PRICE].width,vquotes[j].precision,vPositions[i].Price);
			x+=positionlist_column_items[POSITIONLIST_COL_AVG_PRICE].width+1;
			break;
		case POSITIONLIST_COL_PROFITLOSS:		//product_name
			mvprintw(y,x,"%*.2f",positionlist_column_items[POSITIONLIST_COL_PROFITLOSS].width,GetProfitLoss(vPositions[i].InstrumentID));
			x+=positionlist_column_items[POSITIONLIST_COL_PROFITLOSS].width+1;
			break;
		case POSITIONLIST_COL_MARGIN:		//close
			mvprintw(y,x,"%*.2f",positionlist_column_items[POSITIONLIST_COL_MARGIN].width,vPositions[i].Margin);
			x+=positionlist_column_items[POSITIONLIST_COL_MARGIN].width+1;
			break;
		case POSITIONLIST_COL_AMOUNT:		//product_name
			mvprintw(y,x,"%*.2f",positionlist_column_items[POSITIONLIST_COL_AMOUNT].width,vPositions[i].Balance);
			x+=positionlist_column_items[POSITIONLIST_COL_AMOUNT].width+1;
			break;
		case POSITIONLIST_COL_BUY_VOLUME:		//volume
			mvprintw(y,x,"%*d",positionlist_column_items[POSITIONLIST_COL_BUY_VOLUME].width,vPositions[i].BuyVolume);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_VOLUME].width+1;
			break;
		case POSITIONLIST_COL_BUY_PRICE:		//close
			if(vPositions[i].AvgBuyPrice==DBL_MAX || vPositions[i].AvgBuyPrice==0)
				mvprintw(y,x,"%*c",positionlist_column_items[POSITIONLIST_COL_BUY_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",positionlist_column_items[POSITIONLIST_COL_BUY_PRICE].width,vquotes[j].precision,vPositions[i].AvgBuyPrice);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_PRICE].width+1;
			break;
		case POSITIONLIST_COL_BUY_PROFITLOSS:		//product_name
			mvprintw(y,x,"%*.2f",positionlist_column_items[POSITIONLIST_COL_BUY_PROFITLOSS].width, GetBuyProfitLoss(vPositions[i].InstrumentID));
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_PROFITLOSS].width+1;
			break;
		case POSITIONLIST_COL_BUY_TODAY:		//volume
			mvprintw(y,x,"%*d",positionlist_column_items[POSITIONLIST_COL_BUY_TODAY].width,vPositions[i].TodayBuyVolume);
			x+=positionlist_column_items[POSITIONLIST_COL_BUY_TODAY].width+1;
			break;
		case POSITIONLIST_COL_SELL_VOLUME:		//volume
			mvprintw(y,x,"%*d",positionlist_column_items[POSITIONLIST_COL_SELL_VOLUME].width,vPositions[i].SellVolume);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_VOLUME].width+1;
			break;
		case POSITIONLIST_COL_SELL_PRICE:		//close
			if(vPositions[i].AvgSellPrice==DBL_MAX || vPositions[i].AvgSellPrice==0)
				mvprintw(y,x,"%*c",positionlist_column_items[POSITIONLIST_COL_SELL_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",positionlist_column_items[POSITIONLIST_COL_SELL_PRICE].width,vquotes[j].precision,vPositions[i].AvgSellPrice);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_PRICE].width+1;
			break;
		case POSITIONLIST_COL_SELL_PROFITLOSS:		//product_name
			mvprintw(y,x,"%*.2f",positionlist_column_items[POSITIONLIST_COL_SELL_PROFITLOSS].width, GetSellProfitLoss(vPositions[i].InstrumentID));
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_PROFITLOSS].width+1;
			break;
		case POSITIONLIST_COL_SELL_TODAY:		//volume
			mvprintw(y,x,"%*d",positionlist_column_items[POSITIONLIST_COL_SELL_TODAY].width,vPositions[i].TodaySellVolume);
			x+=positionlist_column_items[POSITIONLIST_COL_SELL_TODAY].width+1;
			break;
		case POSITIONLIST_COL_EXCHANGE_NAME:		//product_name
			mvprintw(y,x,"%-*s",positionlist_column_items[POSITIONLIST_COL_EXCHANGE_NAME].width,vPositions[i].ExchangeID);
			x+=positionlist_column_items[POSITIONLIST_COL_EXCHANGE_NAME].width+1;
			break;
		default:
			break;
		}
	}
}

void positionlist_display_positions()
{
	std::erase_if(vPositions, [](stPosition_t & pos) {
		return !(pos.BuyingVolume!=0 || pos.SellingVolume!=0 || pos.BuyVolume!=0 || pos.SellingVolume != 0);
	});
	for(auto & vPosition : vPositions)
		positionlist_display_position(vPosition.AccID,vPosition.ExchangeID,vPosition.InstrumentID);
}

void positionlist_display_focus()
{
	if(positionlist_curr_line!=0)
		mvchgat(positionlist_curr_line,0,-1,A_REVERSE,0,NULL);
}

void positionlist_scroll_left_1_column()
{
	if(positionlist_curr_col_pos==2)
		return;
	while(mpositionlist_columns[vpositionlist_columns[--positionlist_curr_col_pos]]==false); //	取消所在列的反白显示
	positionlist_redraw();
}
void positionlist_scroll_right_1_column()
{
	if(positionlist_curr_col_pos==sizeof(positionlist_column_items)/sizeof(column_item_t)-1)
		return;
	while(mpositionlist_columns[vpositionlist_columns[++positionlist_curr_col_pos]]==false); //	取消所在列的反白显示
	positionlist_redraw();
}

void positionlist_move_forward_1_line()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
		positionlist_redraw();
		return;
	}
	if(positionlist_curr_line==vPositions.size()-positionlist_curr_pos)	// Already bottom
		return;
	if(positionlist_curr_line!=positionlist_max_lines){
		positionlist_curr_line++;
	}else{
		positionlist_curr_pos++;
	}
	positionlist_redraw();
}

void positionlist_move_backward_1_line()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
		positionlist_redraw();
		return;
	}
	if(positionlist_curr_line==1 && positionlist_curr_pos==0)	// Already top
		return;
	if(positionlist_curr_line>1){
		positionlist_curr_line--;
	}else{
		positionlist_curr_pos--;
	}
	positionlist_redraw();
}

void positionlist_scroll_forward_1_line()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}
	if(positionlist_curr_pos==vPositions.size()-1){	//Already bottom
		return;
	}	
	if(positionlist_curr_line!=1)
		positionlist_curr_line--;
	
	positionlist_curr_pos++;
	positionlist_redraw();
}

void positionlist_scroll_backward_1_line()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}
	if(positionlist_curr_pos==0){	//Already top
		return;
	}
	
	if(positionlist_curr_line!=positionlist_max_lines)
		positionlist_curr_line++;
	
	positionlist_curr_pos--;
	positionlist_redraw();
}

void positionlist_goto_file_top()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}
	if(positionlist_curr_line==1 && positionlist_curr_pos==0)	// Already top
		return;
	if(positionlist_curr_pos==0){
		positionlist_curr_line=1;
	}else{
		positionlist_curr_pos=0;
		positionlist_curr_line=1;
	}
	positionlist_redraw();
}

void positionlist_goto_file_bottom()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}
	if(positionlist_curr_line==vPositions.size()-positionlist_curr_pos)	// Already bottom
		return;
	if(vPositions.size()-positionlist_curr_pos<=positionlist_max_lines){
		positionlist_curr_line=vPositions.size()-positionlist_curr_pos;
	}else{
		positionlist_curr_pos=vPositions.size()-positionlist_max_lines;
		positionlist_curr_line=positionlist_max_lines;
	}
	positionlist_redraw();
}

void positionlist_goto_page_top()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}else{
		positionlist_curr_line=1;
	}
	positionlist_redraw();
}
void positionlist_goto_page_bottom()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}
	if(positionlist_curr_line==vPositions.size()-positionlist_curr_pos)	// Already bottom
		return;
	if(vPositions.size()-positionlist_curr_pos<positionlist_max_lines){
		positionlist_curr_line=vPositions.size()-positionlist_curr_pos;
	}else{
		positionlist_curr_line=positionlist_max_lines;
	}
	positionlist_redraw();
}
void positionlist_goto_page_middle()
{
	if(vPositions.empty())
		return;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
	}
	if(vPositions.size()-positionlist_curr_pos==1)	// Only 1 line
		return;
	if(vPositions.size()-positionlist_curr_pos<positionlist_max_lines){
		positionlist_curr_line=(vPositions.size()-positionlist_curr_pos)/2+1;
	}else{
		positionlist_curr_line=positionlist_max_lines/2+1;
	}
	positionlist_redraw();
}

void positionlist_move_forward_1_page()
{
	int i;
	for(i=0;i<positionlist_max_lines;i++)
		positionlist_scroll_forward_1_line();
}
void positionlist_move_backward_1_page()
{
	int i;
	for(i=0;i<positionlist_max_lines;i++)
		positionlist_scroll_backward_1_line();
}
void positionlist_move_forward_half_page()
{
	int i;
	for(i=0;i<positionlist_max_lines/2;i++)
		positionlist_scroll_forward_1_line();
}
void positionlist_move_backward_half_page()
{
	int i;
	for(i=0;i<positionlist_max_lines/2;i++)
		positionlist_scroll_backward_1_line();
}


// Account List

void acclist_refresh_screen()
{
	int y,x;
	
	if(working_window!=WIN_MONEY)
		return;
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
	acclist_max_lines=y-2;
	acclist_redraw();
}

void acclist_redraw()
{
	erase();
	acclist_display_title();
	acclist_display_accs();
	acclist_display_status();
	acclist_display_focus();
}

void acclist_reset(const char *user)
{
	// Account List Curses
// 	acclist_curr_line=0,acclist_curr_col=1,acclist_max_lines,acclist_max_cols=7;
// 	acclist_curr_pos=0,acclist_curr_col_pos=2;
// 	vAccounts.clear();
// 	acclist_redraw();
}
void acclist_display_title()
{
	int y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_MONEY)
		return;
	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	
	for(auto iter=vacclist_columns.begin();iter!=vacclist_columns.end();iter++,pos++){
		if(macclist_columns[*iter]==false)
			continue;
		if(*iter!=ACCLIST_COL_ACC_ID && *iter!=ACCLIST_COL_ACC_NAME && pos<acclist_curr_col_pos)
			continue;
		if(maxx-x<acclist_column_items[*iter].width)
			break;
		switch(*iter){
		case ACCLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",acclist_column_items[ACCLIST_COL_ACC_ID].width,acclist_column_items[ACCLIST_COL_ACC_ID].name);
			x+=acclist_column_items[ACCLIST_COL_ACC_ID].width;
			break;
		case ACCLIST_COL_ACC_NAME:		//product_name
			mvprintw(y,x,"%-*s",acclist_column_items[ACCLIST_COL_ACC_NAME].width,acclist_column_items[ACCLIST_COL_ACC_NAME].name);
			x+=acclist_column_items[ACCLIST_COL_ACC_NAME].width+1;
			break;
		case ACCLIST_COL_PRE_BALANCE:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_PRE_BALANCE].width+3,acclist_column_items[ACCLIST_COL_PRE_BALANCE].name);
			x+=acclist_column_items[ACCLIST_COL_PRE_BALANCE].width+1;
			break;
		case ACCLIST_COL_MONEY_IN:		//volume
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_MONEY_IN].width+2,acclist_column_items[ACCLIST_COL_MONEY_IN].name);
			x+=acclist_column_items[ACCLIST_COL_MONEY_IN].width+1;
			break;
		case ACCLIST_COL_MONEY_OUT:		//volume
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_MONEY_OUT].width+2,acclist_column_items[ACCLIST_COL_MONEY_OUT].name);
			x+=acclist_column_items[ACCLIST_COL_MONEY_OUT].width+1;
			break;
		case ACCLIST_COL_FROZEN_MARGIN:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_FROZEN_MARGIN].width,acclist_column_items[ACCLIST_COL_FROZEN_MARGIN].name);
			x+=acclist_column_items[ACCLIST_COL_FROZEN_MARGIN].width+1;
			break;
		case ACCLIST_COL_MONEY_FROZEN:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_MONEY_FROZEN].width+4,acclist_column_items[ACCLIST_COL_MONEY_FROZEN].name);
			x+=acclist_column_items[ACCLIST_COL_MONEY_FROZEN].width+1;
			break;
		case ACCLIST_COL_FEE_FROZEN:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_FEE_FROZEN].width,acclist_column_items[ACCLIST_COL_FEE_FROZEN].name);
			x+=acclist_column_items[ACCLIST_COL_FEE_FROZEN].width+1;
			break;
		case ACCLIST_COL_MARGIN:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_MARGIN].width,acclist_column_items[ACCLIST_COL_MARGIN].name);
			x+=acclist_column_items[ACCLIST_COL_MARGIN].width+1;
			break;
		case ACCLIST_COL_FEE:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_FEE].width+3,acclist_column_items[ACCLIST_COL_FEE].name);
			x+=acclist_column_items[ACCLIST_COL_FEE].width+1;
			break;
		case ACCLIST_COL_CLOSE_PROFIT_LOSS:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_CLOSE_PROFIT_LOSS].width+4,acclist_column_items[ACCLIST_COL_CLOSE_PROFIT_LOSS].name);
			x+=acclist_column_items[ACCLIST_COL_CLOSE_PROFIT_LOSS].width+1;
			break;
		case ACCLIST_COL_FLOAT_PROFIT_LOSS:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_FLOAT_PROFIT_LOSS].width+4,acclist_column_items[ACCLIST_COL_FLOAT_PROFIT_LOSS].name);
			x+=acclist_column_items[ACCLIST_COL_FLOAT_PROFIT_LOSS].width+1;
			break;
		case ACCLIST_COL_BALANCE_AVAILABLE:		//close
			mvprintw(y,x,"%*s",acclist_column_items[ACCLIST_COL_BALANCE_AVAILABLE].width+4,acclist_column_items[ACCLIST_COL_BALANCE_AVAILABLE].name);
			x+=acclist_column_items[ACCLIST_COL_BALANCE_AVAILABLE].width+1;
			break;
		case ACCLIST_COL_BROKER_ID:		//close
			mvprintw(y,x,"%-*s",acclist_column_items[ACCLIST_COL_BROKER_ID].width,acclist_column_items[ACCLIST_COL_BROKER_ID].name);
			x+=acclist_column_items[ACCLIST_COL_BROKER_ID].width+1;
			break;
		default:
			break;
		}
	}
}

void acclist_display_status()
{
	int y,x;
	char tradestatus[100],quotestatus[100];
	
	if(working_window!=WIN_MONEY)
		return;
	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (TradeConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(tradestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(tradestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(tradestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(tradestatus,"登录失败");
		break;
	default:
		strcpy(tradestatus,"未知");
		break;
	}
	switch (MarketConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(quotestatus,"正在连接");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(quotestatus,"正在登录");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(quotestatus,"在线");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(quotestatus,"登录失败");
		break;
	default:
		strcpy(quotestatus,"未知");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%ld]",acclist_curr_pos+acclist_curr_line,vAccounts.size());
	mvprintw(y - 1, 15, "%s", status_message);
	mvprintw(y-1,x-25,"%s %s", pTradeRsp->user,tradetime);
}



void acclist_display_acc(const char *szBrokerID,const char *szAccID)
{
	size_t i,y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_MONEY)
		return;
	getmaxyx(stdscr,maxy,maxx);
	for(i=0;i<vAccounts.size();i++)
		if(strcmp(vAccounts[i].BrokerID,szBrokerID)==0 && strcmp(vAccounts[i].AccID,szAccID)==0)
			break;
	if(i<acclist_curr_pos || i>acclist_curr_pos+acclist_max_lines-1)
		return;
	y=i-acclist_curr_pos+1;
	x=0;

	move(y,0);
	clrtoeol();
	
	for(auto iter=vacclist_columns.begin();iter!=vacclist_columns.end();iter++,pos++){
		if(macclist_columns[*iter]==false)
			continue;
		if(*iter!=ACCLIST_COL_ACC_ID && *iter!=ACCLIST_COL_ACC_NAME && pos<acclist_curr_col_pos)
			continue;
		if(maxx-x<acclist_column_items[*iter].width)
			break;
		switch(*iter){
		case ACCLIST_COL_ACC_ID:		//product_id
			mvprintw(y,x,"%-*s",acclist_column_items[ACCLIST_COL_ACC_ID].width,vAccounts[i].AccID);
			x+=acclist_column_items[ACCLIST_COL_ACC_ID].width;
			break;
		case ACCLIST_COL_ACC_NAME:		//product_name
			mvprintw(y,x,"%-*s",acclist_column_items[ACCLIST_COL_ACC_NAME].width,vAccounts[i].AccName);
			x+=acclist_column_items[ACCLIST_COL_ACC_NAME].width+1;
			break;
		case ACCLIST_COL_PRE_BALANCE:		//volume
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_PRE_BALANCE].width,vAccounts[i].PreBalance);
			x+=acclist_column_items[ACCLIST_COL_PRE_BALANCE].width+1;
			break;
		case ACCLIST_COL_MONEY_IN:		//close
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_MONEY_IN].width,vAccounts[i].MoneyIn);
			x+=acclist_column_items[ACCLIST_COL_MONEY_IN].width+1;
			break;
		case ACCLIST_COL_MONEY_OUT:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_MONEY_OUT].width,vAccounts[i].MoneyOut);
			x+=acclist_column_items[ACCLIST_COL_MONEY_OUT].width+1;
			break;
		case ACCLIST_COL_FROZEN_MARGIN:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_FROZEN_MARGIN].width,vAccounts[i].FrozenMargin);
			x+=acclist_column_items[ACCLIST_COL_FROZEN_MARGIN].width+1;
			break;
		case ACCLIST_COL_MONEY_FROZEN:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_MONEY_FROZEN].width,vAccounts[i].MoneyFrozen);
			x+=acclist_column_items[ACCLIST_COL_MONEY_FROZEN].width+1;
			break;
		case ACCLIST_COL_FEE_FROZEN:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_FEE_FROZEN].width,vAccounts[i].FeeFrozen);
			x+=acclist_column_items[ACCLIST_COL_FEE_FROZEN].width+1;
			break;
		case ACCLIST_COL_MARGIN:		//close
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_MARGIN].width,vAccounts[i].Margin);
			x+=acclist_column_items[ACCLIST_COL_MARGIN].width+1;
			break;
		case ACCLIST_COL_FEE:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_FEE].width,vAccounts[i].Fee);
			x+=acclist_column_items[ACCLIST_COL_FEE].width+1;
			break;
		case ACCLIST_COL_CLOSE_PROFIT_LOSS:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_CLOSE_PROFIT_LOSS].width,vAccounts[i].CloseProfitLoss);
			x+=acclist_column_items[ACCLIST_COL_CLOSE_PROFIT_LOSS].width+1;
			break;
		case ACCLIST_COL_FLOAT_PROFIT_LOSS:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_FLOAT_PROFIT_LOSS].width,vAccounts[i].FloatProfitLoss);
			x+=acclist_column_items[ACCLIST_COL_FLOAT_PROFIT_LOSS].width+1;
			break;
		case ACCLIST_COL_BALANCE_AVAILABLE:		//product_name
			mvprintw(y,x,"%*.2f",acclist_column_items[ACCLIST_COL_BALANCE_AVAILABLE].width,vAccounts[i].BalanceAvailable);
			x+=acclist_column_items[ACCLIST_COL_BALANCE_AVAILABLE].width+1;
			break;
		case ACCLIST_COL_BROKER_ID:		//product_name
			mvprintw(y,x,"%-*s",acclist_column_items[ACCLIST_COL_BROKER_ID].width,vAccounts[i].BrokerID);
			x+=acclist_column_items[ACCLIST_COL_BROKER_ID].width+1;
			break;
		default:
			break;
		}
	}
}

void acclist_display_accs()
{
	for(auto & vAccount : vAccounts)
		acclist_display_acc(vAccount.BrokerID,vAccount.AccID);
}

void acclist_display_focus()
{
	if(acclist_curr_line!=0)
		mvchgat(acclist_curr_line,0,-1,A_REVERSE,0,NULL);
}

void acclist_scroll_left_1_column()
{
	if(acclist_curr_col_pos==2)
		return;
	while(macclist_columns[vacclist_columns[--acclist_curr_col_pos]]==false); //	取消所在列的反白显示
	acclist_redraw();
}
void acclist_scroll_right_1_column()
{
	if(acclist_curr_col_pos==sizeof(acclist_column_items)/sizeof(column_item_t)-1)
		return;
	while(macclist_columns[vacclist_columns[++acclist_curr_col_pos]]==false); //	取消所在列的反白显示
	acclist_redraw();
}

void acclist_move_forward_1_line()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
		acclist_redraw();
		return;
	}
	if(acclist_curr_line==vAccounts.size()-acclist_curr_pos)	// Already bottom
		return;
	if(acclist_curr_line!=acclist_max_lines){
		acclist_curr_line++;
	}else{
		acclist_curr_pos++;
	}
	acclist_redraw();
}

void acclist_move_backward_1_line()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
		acclist_redraw();
		return;
	}
	if(acclist_curr_line==1 && acclist_curr_pos==0)	// Already top
		return;
	if(acclist_curr_line>1){
		acclist_curr_line--;
	}else{
		acclist_curr_pos--;
	}
	acclist_redraw();
}

void acclist_scroll_forward_1_line()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}
	if(acclist_curr_pos==vAccounts.size()-1){	//Already bottom
		return;
	}	
	if(acclist_curr_line!=1)
		acclist_curr_line--;
	
	acclist_curr_pos++;
	acclist_redraw();
}

void acclist_scroll_backward_1_line()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}
	if(acclist_curr_pos==0){	//Already top
		return;
	}
	
	if(acclist_curr_line!=acclist_max_lines)
		acclist_curr_line++;
	
	acclist_curr_pos--;
	acclist_redraw();
}

void acclist_goto_file_top()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}
	if(acclist_curr_line==1 && acclist_curr_pos==0)	// Already top
		return;
	if(acclist_curr_pos==0){
		acclist_curr_line=1;
	}else{
		acclist_curr_pos=0;
		acclist_curr_line=1;
	}
	acclist_redraw();
}

void acclist_goto_file_bottom()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}
	if(acclist_curr_line==vAccounts.size()-acclist_curr_pos)	// Already bottom
		return;
	if(vAccounts.size()-acclist_curr_pos<=acclist_max_lines){
		acclist_curr_line=vAccounts.size()-acclist_curr_pos;
	}else{
		acclist_curr_pos=vAccounts.size()-acclist_max_lines;
		acclist_curr_line=acclist_max_lines;
	}
	acclist_redraw();
}

void acclist_goto_page_top()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}else{
		acclist_curr_line=1;
	}
	acclist_redraw();
}
void acclist_goto_page_bottom()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}
	if(acclist_curr_line==vAccounts.size()-acclist_curr_pos)	// Already bottom
		return;
	if(vAccounts.size()-acclist_curr_pos<acclist_max_lines){
		acclist_curr_line=vAccounts.size()-acclist_curr_pos;
	}else{
		acclist_curr_line=acclist_max_lines;
	}
	acclist_redraw();
}
void acclist_goto_page_middle()
{
	if(vAccounts.empty())
		return;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
	}
	if(vAccounts.size()-acclist_curr_pos==1)	// Only 1 line
		return;
	if(vAccounts.size()-acclist_curr_pos<acclist_max_lines){
		acclist_curr_line=(vAccounts.size()-acclist_curr_pos)/2+1;
	}else{
		acclist_curr_line=acclist_max_lines/2+1;
	}
	acclist_redraw();
}

void acclist_move_forward_1_page()
{
	int i;
	for(i=0;i<acclist_max_lines;i++)
		acclist_scroll_forward_1_line();
}
void acclist_move_backward_1_page()
{
	int i;
	for(i=0;i<acclist_max_lines;i++)
		acclist_scroll_backward_1_line();
}
void acclist_move_forward_half_page()
{
	int i;
	for(i=0;i<acclist_max_lines/2;i++)
		acclist_scroll_forward_1_line();
}
void acclist_move_backward_half_page()
{
	int i;
	for(i=0;i<acclist_max_lines/2;i++)
		acclist_scroll_backward_1_line();
}


// Mainboard column settings
void column_settings_refresh_screen()
{
	int i=0,y,x;
	
	if(working_window!=WIN_COLUMN_SETTINGS)
		return;
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
	column_settings_max_lines=y-2;
	column_settings_display_title();
	for(auto iter=mcolumns.begin();iter!=mcolumns.end();iter++,i++)
		display_column(iter->first);
	column_settings_display_status();
	if(column_settings_curr_line!=0)
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
}
void symbol_refresh_screen()
{
	int i,y,x;
	
	if(working_window!=WIN_SYMBOL)
		return;
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
	symbol_max_lines=y-2;
	symbol_display_title();
	std::vector<quotation_t>::iterator iter;
	for(iter=vquotes.begin();iter!=vquotes.end();iter++)
		if(strcmp(iter->product_id,symbol_curr_product_id)==0){
			break;
		}
	i=1;	
	mvprintw(i++,0,"合约名称：%s",STR(iter->product_name).c_str());
	mvprintw(i++,0,"交易所代码：%s",iter->exchange_id);
	mvprintw(i++,0,"交易所名称：%s",STR(iter->exchange_name).c_str());
	mvprintw(i++,0,"合约乘数：%d",iter->Instrument.VolumeMultiple);
	mvprintw(i++,0,"最小变动价位：%.*f",iter->precision,iter->Instrument.PriceTick);
	if(iter->Instrument.ShortMarginRatio==DBL_MAX)
		mvprintw(i++,0,"保证金率：");
	else
		mvprintw(i++,0,"保证金率：%.1f%%",iter->Instrument.ShortMarginRatio*100);
	mvprintw(i++,0,"每手保证金：%.1f",iter->Instrument.ShortMarginRatio*iter->Instrument.VolumeMultiple*iter->DepthMarketData.LastPrice);
	mvprintw(i++,0,"最后交易日：%s",iter->Instrument.ExpireDate);
	mvprintw(i++,0,"品种：%s",iter->Instrument.ProductID);
	switch(iter->Instrument.ProductClass){
	case THOST_FTDC_PC_Futures:
		mvprintw(i++,0,"类别：期货");
		break;
	case THOST_FTDC_PC_Options:
		mvprintw(i++,0,"类别：期权");
		break;
	case THOST_FTDC_PC_Combination:
		mvprintw(i++,0,"类别：组合");
		break;
	case THOST_FTDC_PC_Spot:
		mvprintw(i++,0,"类别：即期");
		break;
	case THOST_FTDC_PC_EFP:
		mvprintw(i++,0,"类别：期转现");
		break;
	case THOST_FTDC_PC_SpotOption:
		mvprintw(i++, 0, "类别：现货期权");
		break;
	case OPENCTP_FTDC_PC_EQUITY:
		mvprintw(i++, 0, "类别：股票");
		break;
	case OPENCTP_FTDC_PC_BOND:
		mvprintw(i++, 0, "类别：债券");
		break;
	case OPENCTP_FTDC_PC_FUND:
		mvprintw(i++, 0, "类别：基金");
		break;
	default:
		mvprintw(i++,0,"类别：未知");
		break;
	}
	if (iter->Instrument.ProductClass == THOST_FTDC_PC_Options) {
		// 期权
		if(iter->Instrument.OptionsType == THOST_FTDC_CP_CallOptions)
			mvprintw(i++, 0, "购沽类型：认购期权");
		else
			mvprintw(i++, 0, "购沽类型：认沽期权");
	}
	symbol_display_status();
}

void display_title()
{
	int y,x,pos=0,maxy,maxx;

	if(working_window!=WIN_MAINBOARD)
		return;
	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	
	for(auto iter=vcolumns.begin();iter!=vcolumns.end();iter++,pos++){
		if(mcolumns[*iter]==false)
			continue;
		if(*iter!=COL_SYMBOL && *iter!=COL_SYMBOL_NAME && pos<curr_col_pos)
			continue;
		if(maxx-x<column_items[*iter].width)
			break;
		switch(*iter){
		case COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL].width,column_items[COL_SYMBOL].name);
			x+=column_items[COL_SYMBOL].width;
			break;
		case COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL_NAME].width,column_items[COL_SYMBOL_NAME].name);
			x+=column_items[COL_SYMBOL_NAME].width+1;
			break;
		case COL_CLOSE:		//close
			mvprintw(y,x,"%*s",column_items[COL_CLOSE].width+2,column_items[COL_CLOSE].name);
			x+=column_items[COL_CLOSE].width+1;
			break;
		case COL_PERCENT:		//close
			mvprintw(y,x,"%*s",column_items[COL_PERCENT].width+2,column_items[COL_PERCENT].name);
			x+=column_items[COL_PERCENT].width+1;
			break;
		case COL_ADVANCE:		//close
			mvprintw(y,x,"%*s",column_items[COL_ADVANCE].width+2,column_items[COL_ADVANCE].name);
			x+=column_items[COL_ADVANCE].width+1;
			break;
		case COL_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_VOLUME].width+2,column_items[COL_VOLUME].name);
			x+=column_items[COL_VOLUME].width+1;
			break;
		case COL_BID_PRICE:		//close
			mvprintw(y,x,"%*s",column_items[COL_BID_PRICE].width+2,column_items[COL_BID_PRICE].name);
			x+=column_items[COL_BID_PRICE].width+1;
			break;
		case COL_BID_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_BID_VOLUME].width+2,column_items[COL_BID_VOLUME].name);
			x+=column_items[COL_BID_VOLUME].width+1;
			break;
		case COL_ASK_PRICE:		//close
			mvprintw(y,x,"%*s",column_items[COL_ASK_PRICE].width+2,column_items[COL_ASK_PRICE].name);
			x+=column_items[COL_ASK_PRICE].width+1;
			break;
		case COL_ASK_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_ASK_VOLUME].width+2,column_items[COL_ASK_VOLUME].name);
			x+=column_items[COL_ASK_VOLUME].width+1;
			break;
		case COL_HIGH_LIMIT:		//high limit
			mvprintw(y, x, "%*s", column_items[COL_HIGH_LIMIT].width+2, column_items[COL_HIGH_LIMIT].name);
			x += column_items[COL_HIGH_LIMIT].width + 1;
			break;
		case COL_LOW_LIMIT:		//low limit
			mvprintw(y, x, "%*s", column_items[COL_LOW_LIMIT].width+2, column_items[COL_LOW_LIMIT].name);
			x += column_items[COL_LOW_LIMIT].width + 1;
			break;
		case COL_OPEN:		//close
			mvprintw(y,x,"%*s",column_items[COL_OPEN].width+2,column_items[COL_OPEN].name);
			x+=column_items[COL_OPEN].width+1;
			break;
		case COL_PREV_SETTLEMENT:		//close
			mvprintw(y,x,"%*s",column_items[COL_PREV_SETTLEMENT].width+2,column_items[COL_PREV_SETTLEMENT].name);
			x+=column_items[COL_PREV_SETTLEMENT].width+1;
			break;
		case COL_TRADE_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_TRADE_VOLUME].width+2,column_items[COL_TRADE_VOLUME].name);
			x+=column_items[COL_TRADE_VOLUME].width+1;
			break;
		case COL_AVERAGE_PRICE:		//close
			mvprintw(y,x,"%*s",column_items[COL_AVERAGE_PRICE].width+2,column_items[COL_AVERAGE_PRICE].name);
			x+=column_items[COL_AVERAGE_PRICE].width+1;
			break;
		case COL_HIGH:		//close
			mvprintw(y,x,"%*s",column_items[COL_HIGH].width+2,column_items[COL_HIGH].name);
			x+=column_items[COL_HIGH].width+1;
			break;
		case COL_LOW:		//close
			mvprintw(y,x,"%*s",column_items[COL_LOW].width+2,column_items[COL_LOW].name);
			x+=column_items[COL_LOW].width+1;
			break;
		case COL_SETTLEMENT:		//close
			mvprintw(y,x,"%*s",column_items[COL_SETTLEMENT].width+2,column_items[COL_SETTLEMENT].name);
			x+=column_items[COL_SETTLEMENT].width+1;
			break;
		case COL_PREV_CLOSE:		//close
			mvprintw(y,x,"%*s",column_items[COL_PREV_CLOSE].width+2,column_items[COL_PREV_CLOSE].name);
			x+=column_items[COL_PREV_CLOSE].width+1;
			break;
		case COL_OPENINT:		//volume
			mvprintw(y,x,"%*s",column_items[COL_OPENINT].width+2,column_items[COL_OPENINT].name);
			x+=column_items[COL_OPENINT].width+1;
			break;
		case COL_PREV_OPENINT:		//volume
			mvprintw(y,x,"%*s",column_items[COL_PREV_OPENINT].width+2,column_items[COL_PREV_OPENINT].name);
			x+=column_items[COL_PREV_OPENINT].width+1;
			break;
		case COL_DATE:		//Date
			mvprintw(y, x, "%-*s", column_items[COL_DATE].width, column_items[COL_DATE].name);
			x += column_items[COL_DATE].width + 1;
			break;
		case COL_TIME:		//Time
			mvprintw(y, x, "%-*s", column_items[COL_TIME].width, column_items[COL_TIME].name);
			x += column_items[COL_TIME].width + 1;
			break;
		case COL_TRADE_DAY:		//Date
			mvprintw(y, x, "%-*s", column_items[COL_TRADE_DAY].width, column_items[COL_TRADE_DAY].name);
			x += column_items[COL_TRADE_DAY].width + 1;
			break;
		case COL_EXCHANGE:		//Exchange
			mvprintw(y, x, "%-*s", column_items[COL_EXCHANGE].width, column_items[COL_EXCHANGE].name);
			x += column_items[COL_EXCHANGE].width + 1;
			break;
		default:
			break;
		}
	}
}
void order_display_title()
{
	if(working_window!=WIN_ORDER)
		return;
	if(order_symbol_index<0)
		return;
	int precision=vquotes[order_symbol_index].precision;
	double high_limit=vquotes[order_symbol_index].DepthMarketData.UpperLimitPrice;
	double low_limit=vquotes[order_symbol_index].DepthMarketData.LowerLimitPrice;
	double PriceTick=vquotes[order_symbol_index].Instrument.PriceTick;
	double close_price=vquotes[order_symbol_index].DepthMarketData.LastPrice;
	double prev_settle=vquotes[order_symbol_index].DepthMarketData.PreSettlementPrice;
	int quantity=vquotes[order_symbol_index].DepthMarketData.Volume;
	int nPosi=0,nBuyPosi=0,nSellPosi=0;
	double AvgBuyPrice = 0, AvgSellPrice = 0;

	double offset;
	double ratio;
	if(close_price==DBL_MAX || close_price==0 || prev_settle==DBL_MAX || prev_settle==0){
		offset=0;
		ratio=0;
	}else{
		offset=close_price-prev_settle;
		ratio=(close_price-prev_settle)/prev_settle*100.0;
	}

	std::vector<stPosition_t>::iterator iter;
	for(iter=vPositions.begin();iter!=vPositions.end();iter++){
		if(strcmp(iter->AccID,order_curr_accname)==0 && strcmp(iter->InstrumentID,vquotes[order_symbol_index].product_id)==0)
			break;
	}
	if(iter!=vPositions.end()){
		nPosi=iter->Volume;
		nBuyPosi=iter->BuyVolume;
		nSellPosi=iter->SellVolume;
		AvgBuyPrice = iter->AvgBuyPrice;
		AvgSellPrice = iter->AvgSellPrice;
	}

// 	int buy_quantity=0,sell_quantity=0;
// 	
// 	std::vector<CThostFtdcOrderField>::iterator iter2;
// 	for(iter2=vOrders.begin();iter2!=vOrders.end();iter2++){
// 		if(strcmp(iter2->InstrumentID,vquotes[order_symbol_index].product_id)!=0)
// 			continue;
// 		if(iter2->OrderStatus==THOST_FTDC_OST_AllTraded || iter2->OrderStatus==THOST_FTDC_OST_Canceled)
// 			continue;
// 		if(iter2->Direction==THOST_FTDC_D_Buy)
// 			buy_quantity+=iter2->VolumeTotal;
// 		else
// 			sell_quantity+=iter2->VolumeTotal;
// 	}
	//int buy_quantity=0,sell_quantity=0,buying_quantity=0,selling_quantity=0,canceling_buy_quantity=0,canceling_sell_quantity=0;
	//
	//std::vector<CThostFtdcOrderField>::iterator iterOrder;
	//std::vector<CThostFtdcInputOrderActionField>::iterator iterCanceling;
	//for(iterOrder=vOrders.begin();iterOrder!=vOrders.end();iterOrder++){
	//	if(strcmp(iterOrder->InvestorID,order_curr_accname)!=0 || strcmp(iterOrder->InstrumentID,vquotes[order_symbol_index].product_id)!=0 || iterOrder->OrderStatus==THOST_FTDC_OST_AllTraded || iterOrder->OrderStatus==THOST_FTDC_OST_Canceled)
	//		continue;
	//	if(iterOrder->OrderStatus==THOST_FTDC_OST_NoTradeQueueing || iterOrder->OrderStatus==THOST_FTDC_OST_PartTradedNotQueueing){
	//		if(iterOrder->Direction==THOST_FTDC_D_Buy)
	//			buy_quantity+=iterOrder->VolumeTotal;
	//		else
	//			sell_quantity+=iterOrder->VolumeTotal;
	//	}else{
	//		if(iterOrder->Direction==THOST_FTDC_D_Buy)
	//			buying_quantity+=iterOrder->VolumeTotal;
	//		else
	//			selling_quantity+=iterOrder->VolumeTotal;
	//	}
	//	
	//	// Canceling
	//	for(iterCanceling=vCancelingOrders.begin();iterCanceling!=vCancelingOrders.end();iterCanceling++){
	//		if(strcmp(iterCanceling->InstrumentID,iterOrder->InstrumentID)==0 && iterCanceling->FrontID==iterOrder->FrontID && iterCanceling->SessionID==iterOrder->SessionID && strcmp(iterCanceling->OrderRef,iterOrder->OrderRef)==0){
	//			if(iterOrder->Direction==THOST_FTDC_D_Buy)
	//				canceling_buy_quantity+=iterOrder->VolumeTotal;
	//			else
	//				canceling_sell_quantity+=iterOrder->VolumeTotal;
	//			break;
	//		}
	//	}
	//}
	//char strbuyorders[100],strsellorders[100];
	//bzero(strbuyorders,sizeof(strbuyorders));
	//bzero(strsellorders,sizeof(strsellorders));
	//if(buy_quantity>0 || buying_quantity>0 || canceling_buy_quantity>0){
	//	if(buy_quantity>0){
	//		sprintf(strbuyorders,"%d",buy_quantity);
	//	}
	//	if(buying_quantity>0){
	//		sprintf(strbuyorders+strlen(strbuyorders),"(%d",buying_quantity);
	//		if(canceling_buy_quantity>0){
	//			sprintf(strbuyorders+strlen(strbuyorders),"/-%d",canceling_buy_quantity);
	//		}
	//		strcat(strbuyorders,")");
	//	}else{
	//		if(canceling_buy_quantity>0){
	//			sprintf(strbuyorders+strlen(strbuyorders),"(-%d)",canceling_buy_quantity);
	//		}
	//	}
	//}
	//if(strlen(strbuyorders)==0)
	//	strcpy(strbuyorders,"0");

	//if(sell_quantity>0 || selling_quantity>0 || canceling_sell_quantity>0){
	//	if(sell_quantity>0){
	//		sprintf(strsellorders,"%d",sell_quantity);
	//	}
	//	if(selling_quantity>0){
	//		sprintf(strsellorders+strlen(strsellorders),"(%d",selling_quantity);
	//		if(canceling_sell_quantity>0){
	//			sprintf(strsellorders+strlen(strsellorders),"/-%d",canceling_sell_quantity);
	//		}
	//		strcat(strsellorders,")");
	//	}else{
	//		if(canceling_sell_quantity>0){
	//			sprintf(strsellorders+strlen(strsellorders),"(-%d)",canceling_sell_quantity);
	//		}
	//	}
	//}
	//if(strlen(strsellorders)==0)
	//	strcpy(strsellorders,"0");
	double PL = 0;
	if (nBuyPosi && close_price != DBL_MAX)
		PL += (close_price - AvgBuyPrice) * nBuyPosi * vquotes[order_symbol_index].Instrument.VolumeMultiple;
	if (nSellPosi && close_price != DBL_MAX)
		PL += (AvgSellPrice - close_price) * nSellPosi * vquotes[order_symbol_index].Instrument.VolumeMultiple;
	if(nBuyPosi!=0 && nSellPosi!=0){
		mvprintw(0,0,"%s  %.*f(%.1f%%)  持仓:%d*(%d/%d)  盈亏:%.2f\n",
			STR(vquotes[order_symbol_index].product_name).c_str(),	// 合约
			precision,
			offset,	// 涨跌
			ratio,	// 涨跌幅
			//quantity,	// 成交量
			//order_curr_accname,	// 帐户
			nPosi,	// 持仓
			nBuyPosi,	// 买持仓
			nSellPosi==0?0:-1*nSellPosi,	// 卖持仓
			//strbuyorders,
			//strsellorders,
// 			buy_quantity,	//挂买量
// 			sell_quantity==0?0:-1*sell_quantity,	//挂卖量
			PL);	// 盈亏
	}else{
		mvprintw(0,0,"%s  %.*f(%.1f%%)  持仓:%d  盈亏:%.2f\n",
			STR(vquotes[order_symbol_index].product_name).c_str(),	// 合约
			precision,
			offset,	// 涨跌
			ratio,	// 涨跌幅
			//quantity,	// 成交量
			//order_curr_accname,	// 帐户
			nPosi,	// 持仓
			//strbuyorders,
			//strsellorders,
// 			buy_quantity,	//挂买量
// 			sell_quantity==0?0:-1*sell_quantity,	//挂卖量
			PL);	// 盈亏
	}
	mvprintw(1,0,"%12s %12s %12s %12s %12s\n","买入","叫买","价格","叫卖","卖出");
}

void column_settings_display_title()
{
	if(working_window!=WIN_COLUMN_SETTINGS)
		return;
	move(0,0);
	clrtoeol();
	printw("使用空格键选择显示项，使用+/-调整显示顺序");
}
void symbol_display_title()
{
	int y,x;

	if(working_window!=WIN_SYMBOL)
		return;
	move(0,0);
	clrtoeol();
	getmaxyx(stdscr,y,x);
	mvprintw(0,x/2-4,"%s",symbol_curr_product_id);
}

void work_thread()
{
	// Init screen
	init_screen();
	
	// Run
	while (true) {
		sem.wait();
		lock.lock();
		auto task = vTasks.begin();
		if (task == vTasks.end()) {
			lock.unlock();
			continue;
		}
		(*task)();
		vTasks.erase(task);
		lock.unlock();
	}

	// End screen
	endwin();
}

void time_thread()
{
	size_t seconds = 0;
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		seconds++;
		if (seconds_delayed++ >= 3)
			post_task(std::bind(HandleStatusClear));
		if (seconds % 3 == 0)
			post_task(std::bind(HandleQueryAccount));
		post_task(std::bind(HandleTickTimeout));
	}
}

void on_key_pressed(int ch)
{
	int r=0;
	switch(working_window){
	case WIN_MAINBOARD:
		r=on_key_pressed_mainboard(ch);
		break;
	case WIN_ORDER:
		r=on_key_pressed_order(ch);
		break;
	case WIN_FAVORITE:
		r=on_key_pressed_favorite(ch);
		break;
	case WIN_COLUMN_SETTINGS:
		r=on_key_pressed_column_settings(ch);
		break;
	case WIN_SYMBOL:
		r=on_key_pressed_symbol(ch);
		break;
	case WIN_ORDERLIST:
		r=on_key_pressed_orderlist(ch);
		break;
	case WIN_FILLLIST:
		r=on_key_pressed_filllist(ch);
		break;
	case WIN_POSITION:
		r=on_key_pressed_positionlist(ch);
		break;
	case WIN_MONEY:
		r=on_key_pressed_acclist(ch);
		break;
	case WIN_MESSAGE:
		break;
	default:
		r=0;
		break;
	}

	refresh();
	if(corner_win){
		corner_redraw();
		wrefresh(corner_win);
	}
	if(order_corner_win){
		order_corner_redraw();
		wrefresh(order_corner_win);
	}

	if(r<0){
		endwin();
		exit(0);
	}
}
int input_parse(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;

	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
	case '\'':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case '\'':
			*cmd='\'';
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
	default:
		*cmd=*p;
		break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';

	return 0;
}

int on_key_pressed_mainboard(int ch)
{
	int Num,Cmd;

	if(corner_win){
		on_key_pressed_corner(ch);
		return 0;
	}
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	
	if(input_parse(&Num,&Cmd)<0)
		return 0;

	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
		break;
	case 32:	// Space
		goto_symbol_window_from_mainboard();
		return 0;
	case KEYBOARD_F(4):
	case '4':
	case KEYBOARD_ENTER:	// Enter
		goto_order_window_from_mainboard();
		return 0;
	case KEYBOARD_F(5):
	case '5':
		goto_positionlist_window_from_mainboard();
		return 0;
	case KEYBOARD_F(6):
	case '6':
		goto_acclist_window_from_mainboard();
		return 0;
	case KEYBOARD_F(7):
	case '7':
		goto_orderlist_window_from_mainboard();
		return 0;
	case KEYBOARD_F(8):
	case '8':
		goto_filllist_window_from_mainboard();
		return 0;
	case KEYBOARD_F(9):
	case '9':
		return 0;
	case 'v':
		goto_column_settings_window_from_mainboard();
		return 0;
	case KEYBOARD_REFRESH:		// ^L
		refresh_screen();
		break;
	case '-':
// 		mainboard_move_up_1_line();
		break;
	case '+':
// 		mainboard_move_down_1_line();
		break;
//	case 'h':
	//case KEY_LEFT:
	//	if(curr_col!=1){
	//		curr_col--; //	取消所在列的反白显示
	//	}
	//	break;
	case 'j':
	case KEYBOARD_DOWN:		// DOWN
	case KEYBOARD_NEXT:		// ^N
		for(;Num>0;Num--)
			move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case KEYBOARD_UP:		// UP
		for(;Num>0;Num--)
			move_backward_1_line();
		break;
//	case 'l':
	//case KEY_RIGHT:
	//	if(curr_col!=max_cols){
	//		curr_col++; //	取消所在列的反白显示
	//	}
	//	break;
	case 'f':	// forward 1 page
	case KEYBOARD_CTRL_F:		// ^F
	case KEYBOARD_PAGEDOWN:
		for(;Num>0;Num--)
			move_forward_1_page();
		break;
	case 's':	// sort Asc by selected column
		break;
	case 'S':	// sort Desc by selected column
		break;
	case 'b':
	case KEYBOARD_CTRL_B:		// ^B
	case KEYBOARD_PAGEUP:
		for(;Num>0;Num--)
			move_backward_1_page();
		break;
	case 'u':	// backward harf page
	case KEYBOARD_CTRL_U:		// ^U
		for(;Num>0;Num--)
			move_backward_half_page();
		break;
	case 'd':	// forward harf page
	case KEYBOARD_CTRL_D:		// ^D
		for(;Num>0;Num--)
			move_forward_half_page();
		break;
	case 'e':	// forward 1 line
	case KEYBOARD_CTRL_E:		// ^E
		for(;Num>0;Num--)
			scroll_forward_1_line();
		break;
	case 'y':	// backward 1 line
	case KEYBOARD_CTRL_Y:		// ^Y
		for(;Num>0;Num--)
			scroll_backward_1_line();
		break;
	case 'G':	// bottom line
	case KEY_END:
		goto_file_bottom();
		break;
	case 'g':	// top line
	case KEY_HOME:
		goto_file_top();
		break;
	case 'H':	// screen top
		goto_page_top();
		break;
	case 'L':	// screen bottom
		goto_page_bottom();
		break;
	case 'M':	// screen middle
		goto_page_middle();
		break;
	case 'h':	// scroll to left
	case KEYBOARD_LEFT:
		for(;Num>0;Num--)
			scroll_left_1_column();
		break;
	case 'l':	// scroll to right
	case KEYBOARD_RIGHT:
		for(;Num>0;Num--)
			scroll_right_1_column();
		break;
	case '^':	// scroll to begin
		break;
	case '$':	// scroll to end
		break;
// 	case '+':	// enlarge column width
// 		break;
// 	case '-':	// lessen column width
// 		break;
	case '/':
	case '?':
		corner_reset();
		corner_refresh_screen();
		break;
	default:
		break;
	}
	display_status();

	return 0;
}
int goto_mainboard_window_from_order()
{
	char *ppInstrumentID[1];

	working_window=WIN_MAINBOARD;
	refresh_screen();

	unsubscribe(order_symbol_index);
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}

	return 0;
}
int goto_orderlist_window_from_order()
{
	
	working_window=WIN_ORDERLIST;
	orderlist_refresh_screen();
	
	unsubscribe(order_symbol_index);
	
	return 0;
}

int goto_filllist_window_from_order()
{
	
	working_window=WIN_FILLLIST;
	filllist_refresh_screen();
	
	unsubscribe(order_symbol_index);
	
	return 0;
}

int goto_positionlist_window_from_order()
{
	
	working_window=WIN_POSITION;
	positionlist_refresh_screen();
	
	unsubscribe(order_symbol_index);
	
	return 0;
}

int goto_acclist_window_from_order()
{
	
	working_window=WIN_MONEY;
	acclist_refresh_screen();
	
	unsubscribe(order_symbol_index);
	
	return 0;
}

int goto_mainboard_window_from_orderlist()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}
	
	return 0;
}

int goto_mainboard_window_from_filllist()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}
	
	return 0;
}

int goto_mainboard_window_from_positionlist()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}
	
	return 0;
}

int goto_mainboard_window_from_acclist()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}
	
	return 0;
}

int goto_mainboard_window_from_log()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}
	
	return 0;
}

int goto_order_window_from_orderlist()
{
	if(vOrders.empty())
		return 0;
	if(orderlist_curr_line==0){	// first select
		orderlist_curr_line=1;
		mvchgat(orderlist_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	size_t i;
	for(i=0;i<vquotes.size();i++){
		if(strcmp(vquotes[i].product_id,vOrders[orderlist_curr_pos+orderlist_curr_line-1].InstrumentID)==0)
			break;
	}
	if(i==vquotes.size())
		return 0;
	order_symbol_index=i;
	working_window=WIN_ORDER;
	order_curr_price=0;
	order_page_top_price=0;
	strcpy(order_curr_accname,vOrders[orderlist_curr_pos+orderlist_curr_line-1].InvestorID);
	order_refresh_screen();
	order_centralize_current_price();
	subscribe(order_symbol_index);
	
	return 0;
}

int goto_filllist_window_from_orderlist()
{
	
	working_window=WIN_FILLLIST;
	filllist_refresh_screen();
		
	return 0;
}

int goto_positionlist_window_from_orderlist()
{
	
	working_window=WIN_POSITION;
	positionlist_refresh_screen();
	
	return 0;
}

int goto_acclist_window_from_orderlist()
{
	
	working_window=WIN_MONEY;
	acclist_refresh_screen();
	
	return 0;
}

int goto_mainboard_window_from_column_settings()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}	
	
	return 0;
}
int goto_mainboard_window_from_symbol()
{
	char *ppInstrumentID[1];
	
	working_window=WIN_MAINBOARD;
	refresh_screen();
	
	for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++){
		if(vquotes[i].subscribed){
			ppInstrumentID[0]=vquotes[i].product_id;
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				return -1;
			vquotes[i].subscribed=true;
		}
	}	
	
	return 0;
}

int input_parse_order(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int on_key_pressed_order(int ch)
{
	int Num,Cmd;
	
	if(order_corner_win){
		order_on_key_pressed_corner(ch);
		return 0;
	}
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;

	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_F(1):
	case '1':
	case KEYBOARD_ESC:	// ESC
		if (order_is_moving){
			order_is_moving=0;
			return 0;
		}
		goto_mainboard_window_from_order();
		return 0;
	case KEYBOARD_F(5):
	case '5':
		goto_positionlist_window_from_order();
		return 0;
	case KEYBOARD_F(6):
	case '6':
		goto_acclist_window_from_order();
		return 0;
	case KEYBOARD_F(7):
	case '7':
		goto_orderlist_window_from_order();
		return 0;
	case KEYBOARD_F(8):
	case '8':
		goto_filllist_window_from_order();
		return 0;
	case KEYBOARD_F(9):
	case '9':
		return 0;
	case 32:	// Space
		order_centralize_current_price();
		break;
	case KEYBOARD_REFRESH:		// ^L
		order_refresh_screen();
		break;
	case 'h':
	case KEYBOARD_LEFT:
		if(order_is_moving)
			return 0;
		order_curr_col=0;
		order_redraw();
// 		if(curr_col!=1){
// 			curr_col--;
// 		}
		break;
	case 'j':
	case KEYBOARD_DOWN:		// ^N
	case KEYBOARD_ENTER:	// Enter
	case '+':
	case KEYBOARD_NEXT:
		for(;Num>0;Num--)
			order_move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case '-':
	case KEYBOARD_UP:
		for(;Num>0;Num--)
			order_move_backward_1_line();
		break;
	case 'l':
	case KEYBOARD_RIGHT:
		if(order_is_moving)
			return 0;
		order_curr_col=1;
		order_redraw();
// 		if(curr_col!=max_cols){
// 			curr_col++;
// 		}
		break;
	case 'f':	// forward 1 page
	case KEYBOARD_CTRL_F:		// ^F
	case KEYBOARD_PAGEDOWN:
		for(;Num>0;Num--)
			order_move_forward_1_page();
		break;
	case 'p':
		if (order_is_moving) 
			order_move_complete();
		break;
	case 'i':
		if(order_curr_col==0)
			order_buy_at_limit(Num);
		else
			order_sell_at_limit(Num);
		break;
	case 'I':
		if(order_curr_col==0)
			order_buy_at_market(Num);
		else
			order_sell_at_market(Num);
		break;
	//case 'b':	// buy at select
	//	//for(;Num>0;Num--)
	//		//order_buy_at_limit(Num);
	//	break;
	case 'B':	// buy at market
		//for(;Num>0;Num--)
			//order_buy_at_market(Num);
		break;
	case 's':	// sell at select
		//for(;Num>0;Num--)
			//order_sell_at_limit(Num);
		break;
	case 'S':	// sell at market
		//for(;Num>0;Num--)
			//order_sell_at_market(Num);
		break;
	case 'r':	// sell at select
		//for(;Num>0;Num--)
			order_revert_at_limit();
		break;
	case 'R':	// sell at market
		//for(;Num>0;Num--)
			order_revert_at_market();
		break;
	case 'u':	// cancel orders
		order_cancel_orders();
		break;
	case 'U':	// cancel all orders
		order_cancel_all_orders();
		break;
	case 'x':	// movie orders
		order_move_orders();
		break;
	case 'b':
	case KEYBOARD_CTRL_B:		// ^B
	case KEYBOARD_PAGEUP:
		for(;Num>0;Num--)
			order_move_backward_1_page();
		break;
	case KEYBOARD_CTRL_U:	// ^U backward harf page
		for(;Num>0;Num--)
			order_move_backward_half_page();
		break;
	case KEYBOARD_CTRL_D:		// ^D forward harf page
		for(;Num>0;Num--)
			order_move_forward_half_page();
		break;
	case 'e':
	case KEYBOARD_CTRL_E:		// ^E forward 1 line
		for(;Num>0;Num--)
			order_scroll_forward_1_line();
		break;
	case 'y':
	case KEYBOARD_CTRL_Y:		// ^Y backward 1 line
		for(;Num>0;Num--)
			order_scroll_backward_1_line();
		break;
	case 'G':	// bottom line
	case KEY_END:
		order_goto_file_bottom();
		break;
	case 'g':	// top line
	case KEY_HOME:
		order_goto_file_top();
		break;
	case 'H':	// screen top
		order_goto_page_top();
		break;
	case 'L':	// screen bottom
		order_goto_page_bottom();
		break;
	case 'M':	// screen middle
		order_goto_page_middle();
		break;
	case '/':
	case '?':
		order_corner_reset();
		order_corner_refresh_screen();
		break;
	case '\'':
		order_open_last_symbol();
		break;
	default:
		break;
	}
	order_display_status();

	return 0;
}
void order_open_last_symbol()
{
	for(size_t i=0;i<vquotes.size();i++){
		if(strcmp(order_last_symbol,vquotes[i].product_id)==0){
			unsubscribe(order_symbol_index);
			strcpy(order_last_symbol,vquotes[order_symbol_index].product_id);
			order_symbol_index=i;
			order_curr_price=0;
			order_page_top_price=0;
			order_refresh_screen();
			order_centralize_current_price();
			subscribe(order_symbol_index);
			break;
		}
	}
}
int input_parse_favorite(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}
int on_key_pressed_favorite(int ch)
{
	return 0;
}
int input_parse_column_settings(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}
int on_key_pressed_column_settings(int ch)
{
	int Num,Cmd;
	
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
		goto_mainboard_window_from_column_settings();
		return 0;
	case 32:	// Space
		column_settings_select_column();
		break;
	case KEYBOARD_ENTER:	// Enter
		goto_mainboard_window_from_column_settings();
		return 0;
	case KEYBOARD_REFRESH:		// ^L
		column_settings_refresh_screen();
		break;
	case 'j':
	case KEYBOARD_DOWN:		// ^N
	case KEYBOARD_NEXT:
		column_settings_move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case KEYBOARD_UP:
		column_settings_move_backward_1_line();
		break;
	case '-':
		column_settings_move_up_1_line();
		break;
	case '+':
		column_settings_move_down_1_line();
		break;
	case 'h':	// sub the width
		break;
	case 'l':	// enlarge the width
		break;
	default:
		break;
	}
	column_settings_display_status();
	return 0;
}
int input_parse_orderlist(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int on_key_pressed_orderlist(int ch)
{
	int Num,Cmd;
	
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
	case KEYBOARD_F(1):
	case '1':
		goto_mainboard_window_from_orderlist();
		return 0;
	case KEYBOARD_F(4):
	case '4':
	case KEYBOARD_ENTER:	// Enter
		goto_order_window_from_orderlist();
		return 0;
	case KEYBOARD_F(5):
	case '5':
		goto_positionlist_window_from_orderlist();
		return 0;
	case KEYBOARD_F(6):
	case '6':
		goto_acclist_window_from_orderlist();
		return 0;
	case KEYBOARD_F(8):
	case '8':
		goto_filllist_window_from_orderlist();
		return 0;
	case KEYBOARD_F(9):
	case '9':
		return 0;
	case 32:	// Space
		break;
	case KEYBOARD_REFRESH:		// ^L
		orderlist_refresh_screen();
		break;
//	case 'h':
// 		if(curr_col!=1){
// 			curr_col--;
// 		}
		break;
	case 'j':
	case KEYBOARD_DOWN:		// ^N
	case '+':
	case KEYBOARD_NEXT:
		orderlist_move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case '-':
	case KEYBOARD_UP:
		orderlist_move_backward_1_line();
		break;
//	case 'l':
// 		if(curr_col!=max_cols){
// 			curr_col++;
// 		}
		break;
	case 'f':	// forward 1 page
	case KEYBOARD_CTRL_F:		// ^F
	case KEYBOARD_PAGEDOWN:
		orderlist_move_forward_1_page();
		break;
	case 'b':
	case KEYBOARD_CTRL_B:		// ^B
	case KEYBOARD_PAGEUP:
		orderlist_move_backward_1_page();
		break;
	case 'u':	// backward harf page
	case KEYBOARD_CTRL_U:		// ^U
		orderlist_move_backward_half_page();
		break;
	case 'd':	// forward harf page
	case KEYBOARD_CTRL_D:		// ^D
		orderlist_move_forward_half_page();
		break;
	case 'e':	// forward 1 line
	case KEYBOARD_CTRL_E:		// ^E
		orderlist_scroll_forward_1_line();
		break;
	case 'y':	// backward 1 line
	case KEYBOARD_CTRL_Y:		// ^Y
		orderlist_scroll_backward_1_line();
		break;
	case 'G':	// bottom line
	case KEY_END:
		orderlist_goto_file_bottom();
		break;
	case 'g':	// top line
	case KEY_HOME:
		orderlist_goto_file_top();
		break;
	case 'H':	// screen top
		orderlist_goto_page_top();
		break;
	case 'L':	// screen bottom
		orderlist_goto_page_bottom();
		break;
	case 'M':	// screen middle
		orderlist_goto_page_middle();
		break;
	case 'h':	// scroll to left
	case KEYBOARD_LEFT:
		orderlist_scroll_left_1_column();
		break;
	case 'l':	// scroll to right
	case KEYBOARD_RIGHT:
		orderlist_scroll_right_1_column();
		break;
	default:
		break;
	}
	orderlist_display_status();

	return 0;
}
int input_parse_filllist(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int on_key_pressed_filllist(int ch)
{
	int Num,Cmd;
	
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
	case KEYBOARD_F(1):
	case '1':
		goto_mainboard_window_from_filllist();
		return 0;
	case KEYBOARD_F(4):
	case '4':
	case KEYBOARD_ENTER:	// Enter
		goto_order_window_from_filllist();
		return 0;
	case KEYBOARD_F(5):
	case '5':
		goto_positionlist_window_from_filllist();
		return 0;
	case KEYBOARD_F(6):
	case '6':
		goto_acclist_window_from_filllist();
		return 0;
	case KEYBOARD_F(7):
	case '7':
		goto_orderlist_window_from_filllist();
		return 0;
	case KEYBOARD_F(9):
	case '9':
		return 0;
	case 32:	// Space
		break;
	case KEYBOARD_REFRESH:		// ^L
		filllist_refresh_screen();
		break;
//	case 'h':
// 		if(curr_col!=1){
// 			curr_col--;
// 		}
		break;
	case 'j':
	case KEYBOARD_DOWN:		// ^N
	case '+':
	case KEYBOARD_NEXT:
		filllist_move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case '-':
	case KEYBOARD_UP:
		filllist_move_backward_1_line();
		break;
//	case 'l':
// 		if(curr_col!=max_cols){
// 			curr_col++;
// 		}
		break;
	case 'f':	// forward 1 page
	case KEYBOARD_CTRL_F:		// ^F
	case KEYBOARD_PAGEDOWN:
		filllist_move_forward_1_page();
		break;
	case 'b':
	case KEYBOARD_CTRL_B:		// ^B
	case KEYBOARD_PAGEUP:
		filllist_move_backward_1_page();
		break;
	case 'u':	// backward harf page
	case KEYBOARD_CTRL_U:		// ^U
		filllist_move_backward_half_page();
		break;
	case 'd':	// forward harf page
	case KEYBOARD_CTRL_D:		// ^D
		filllist_move_forward_half_page();
		break;
	case 'e':	// forward 1 line
	case KEYBOARD_CTRL_E:		// ^E
		filllist_scroll_forward_1_line();
		break;
	case 'y':	// backward 1 line
	case KEYBOARD_CTRL_Y:		// ^Y
		filllist_scroll_backward_1_line();
		break;
	case 'G':	// bottom line
	case KEY_END:
		filllist_goto_file_bottom();
		break;
	case 'g':	// top line
	case KEY_HOME:
		filllist_goto_file_top();
		break;
	case 'H':	// screen top
		filllist_goto_page_top();
		break;
	case 'L':	// screen bottom
		filllist_goto_page_bottom();
		break;
	case 'M':	// screen middle
		filllist_goto_page_middle();
		break;
	case 'h':	// scroll to left
	case KEYBOARD_LEFT:
		filllist_scroll_left_1_column();
		break;
	case 'l':	// scroll to right
	case KEYBOARD_RIGHT:
		filllist_scroll_right_1_column();
		break;
	default:
		break;
	}
	filllist_display_status();

	return 0;
}
int input_parse_positionlist(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int on_key_pressed_positionlist(int ch)
{
	int Num,Cmd;
	
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
	case KEYBOARD_F(1):
	case '1':
		goto_mainboard_window_from_positionlist();
		return 0;
	case KEYBOARD_F(4):
	case '4':
	case KEYBOARD_ENTER:	// Enter
		goto_order_window_from_positionlist();
		return 0;
	case KEYBOARD_F(6):
	case '6':
		goto_acclist_window_from_positionlist();
		return 0;
	case KEYBOARD_F(7):
	case '7':
		goto_orderlist_window_from_positionlist();
		return 0;
	case KEYBOARD_F(8):
	case '8':
		goto_filllist_window_from_positionlist();
		return 0;
	case KEYBOARD_F(9):
	case '9':
		return 0;
	case 32:	// Space
		break;
	case KEYBOARD_REFRESH:		// ^L
		positionlist_refresh_screen();
		break;
//	case 'h':
// 		if(curr_col!=1){
// 			curr_col--;
// 		}
		break;
	case 'j':
	case KEYBOARD_DOWN:		// ^N
	case '+':
	case KEYBOARD_NEXT:
		positionlist_move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case '-':
	case KEYBOARD_UP:
		positionlist_move_backward_1_line();
		break;
//	case 'l':
// 		if(curr_col!=max_cols){
// 			curr_col++;
// 		}
		break;
	case 'f':	// forward 1 page
	case KEYBOARD_CTRL_F:		// ^F
	case KEYBOARD_PAGEDOWN:
		positionlist_move_forward_1_page();
		break;
	case 'b':
	case KEYBOARD_CTRL_B:		// ^B
	case KEYBOARD_PAGEUP:
		positionlist_move_backward_1_page();
		break;
	case 'u':	// backward harf page
	case KEYBOARD_CTRL_U:		// ^U
		positionlist_move_backward_half_page();
		break;
	case 'd':	// forward harf page
	case KEYBOARD_CTRL_D:		// ^D
		positionlist_move_forward_half_page();
		break;
	case 'e':	// forward 1 line
	case 5:		// ^E
		positionlist_scroll_forward_1_line();
		break;
	case 'y':	// backward 1 line
	case KEYBOARD_CTRL_Y:		// ^Y
		positionlist_scroll_backward_1_line();
		break;
	case 'G':	// bottom line
	case KEY_END:
		positionlist_goto_file_bottom();
		break;
	case 'g':	// top line
	case KEY_HOME:
		positionlist_goto_file_top();
		break;
	case 'H':	// screen top
		positionlist_goto_page_top();
		break;
	case 'L':	// screen bottom
		positionlist_goto_page_bottom();
		break;
	case 'M':	// screen middle
		positionlist_goto_page_middle();
		break;
	case 'h':	// scroll to left
	case KEYBOARD_LEFT:
		positionlist_scroll_left_1_column();
		break;
	case 'l':	// scroll to right
	case KEYBOARD_RIGHT:
		positionlist_scroll_right_1_column();
		break;
	default:
		break;
	}
	positionlist_display_status();

	return 0;
}
int input_parse_acclist(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int on_key_pressed_acclist(int ch)
{
	int Num,Cmd;
	
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
	case KEYBOARD_F(1):
	case '1':
		goto_mainboard_window_from_acclist();
		return 0;
	case KEYBOARD_F(4):
	case '4':
		goto_order_window_from_acclist();
		return 0;
	case KEYBOARD_F(5):
	case '5':
	case KEYBOARD_ENTER:	// Enter
		goto_positionlist_window_from_acclist();
		return 0;
	case KEYBOARD_F(7):
	case '7':
		goto_orderlist_window_from_acclist();
		return 0;
	case KEYBOARD_F(8):
	case '8':
		goto_filllist_window_from_acclist();
		return 0;
	case KEYBOARD_F(9):
	case '9':
		return 0;
	case 32:	// Space
		break;
	case KEYBOARD_REFRESH:		// ^L
		acclist_refresh_screen();
		break;
//	case 'h':
// 		if(curr_col!=1){
// 			curr_col--;
// 		}
		break;
	case 'j':
	case KEYBOARD_DOWN:		// ^N
	case '+':
	case KEYBOARD_NEXT:
		acclist_move_forward_1_line();
		break;
	case 'k':
	case KEYBOARD_PREVIOUS:		// ^P
	case '-':
	case KEYBOARD_UP:
		acclist_move_backward_1_line();
		break;
//	case 'l':
// 		if(curr_col!=max_cols){
// 			curr_col++;
// 		}
		break;
	case 'f':	// forward 1 page
	case KEYBOARD_CTRL_F:		// ^F
	case KEYBOARD_PAGEDOWN:
		acclist_move_forward_1_page();
		break;
	case 'b':
	case KEYBOARD_CTRL_B:		// ^B
	case KEYBOARD_PAGEUP:
		acclist_move_backward_1_page();
		break;
	case 'u':	// backward harf page
	case KEYBOARD_CTRL_U:		// ^U
		acclist_move_backward_half_page();
		break;
	case 'd':	// forward harf page
	case KEYBOARD_CTRL_D:		// ^D
		acclist_move_forward_half_page();
		break;
	case 'e':	// forward 1 line
	case KEYBOARD_CTRL_E:		// ^E
		acclist_scroll_forward_1_line();
		break;
	case 'y':	// backward 1 line
	case KEYBOARD_CTRL_Y:		// ^Y
		acclist_scroll_backward_1_line();
		break;
	case 'G':	// bottom line
	case KEY_END:
		acclist_goto_file_bottom();
		break;
	case 'g':	// top line
	case KEY_HOME:
		acclist_goto_file_top();
		break;
	case 'H':	// screen top
		acclist_goto_page_top();
		break;
	case 'L':	// screen bottom
		acclist_goto_page_bottom();
		break;
	case 'M':	// screen middle
		acclist_goto_page_middle();
		break;
	case 'h':	// scroll to left
	case KEYBOARD_LEFT:
		acclist_scroll_left_1_column();
		break;
	case 'l':	// scroll to right
	case KEYBOARD_RIGHT:
		acclist_scroll_right_1_column();
		break;
	default:
		break;
	}
	acclist_display_status();

	return 0;
}
int input_parse_log(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int goto_order_window_from_filllist()
{
	if(vFilledOrders.empty())
		return 0;
	if(filllist_curr_line==0){	// first select
		filllist_curr_line=1;
		mvchgat(filllist_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	int i;
	for(i=0;i<vquotes.size();i++){
		if(strcmp(vquotes[i].product_id,vFilledOrders[filllist_curr_pos+filllist_curr_line-1].InstrumentID)==0)
			break;
	}
	if(i==vquotes.size())
		return 0;
	order_symbol_index=i;
	working_window=WIN_ORDER;
	order_curr_price=0;
	order_page_top_price=0;
	strcpy(order_curr_accname,vFilledOrders[filllist_curr_pos+filllist_curr_line-1].InvestorID);
	order_refresh_screen();
	order_centralize_current_price();
	subscribe(order_symbol_index);
	
	return 0;
}

int goto_order_window_from_positionlist()
{
	if(vPositions.empty())
		return 0;
	if(positionlist_curr_line==0){	// first select
		positionlist_curr_line=1;
		mvchgat(positionlist_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	int i;
	for(i=0;i<vquotes.size();i++){
		if(strcmp(vquotes[i].product_id,vPositions[positionlist_curr_pos+positionlist_curr_line-1].InstrumentID)==0)
			break;
	}
	if(i==vquotes.size())
		return 0;
	order_symbol_index=i;
	working_window=WIN_ORDER;
	order_curr_price=0;
	order_page_top_price=0;
	strcpy(order_curr_accname,vPositions[positionlist_curr_pos+positionlist_curr_line-1].AccID);
	order_refresh_screen();
	order_centralize_current_price();
	subscribe(order_symbol_index);
	
	return 0;
}

int goto_order_window_from_acclist()
{
	
	return 0;
}

int goto_order_window_from_log()
{
	
	return 0;
}

int goto_orderlist_window_from_filllist()
{
	
	working_window=WIN_ORDERLIST;
	orderlist_refresh_screen();
	
	return 0;
}

int goto_positionlist_window_from_filllist()
{
	
	working_window=WIN_POSITION;
	positionlist_refresh_screen();
	
	return 0;
}

int goto_acclist_window_from_filllist()
{
	
	working_window=WIN_MONEY;
	acclist_refresh_screen();
	
	return 0;
}

int goto_orderlist_window_from_positionlist()
{
	
	working_window=WIN_ORDERLIST;
	orderlist_refresh_screen();
	
	return 0;
}

int goto_orderlist_window_from_acclist()
{
	
	working_window=WIN_ORDERLIST;
	orderlist_refresh_screen();
	
	return 0;
}

int goto_orderlist_window_from_log()
{
	
	working_window=WIN_ORDERLIST;
	orderlist_refresh_screen();
	
	return 0;
}


int goto_filllist_window_from_positionlist()
{
	
	working_window=WIN_FILLLIST;
	filllist_refresh_screen();
	
	return 0;
}

int goto_acclist_window_from_positionlist()
{
	
	working_window=WIN_MONEY;
	acclist_refresh_screen();
	
	return 0;
}

int goto_acclist_window_from_log()
{
	
	working_window=WIN_MONEY;
	acclist_refresh_screen();
	
	return 0;
}

int goto_filllist_window_from_acclist()
{
	
	working_window=WIN_FILLLIST;
	filllist_refresh_screen();
	
	return 0;
}

int goto_filllist_window_from_log()
{
	
	working_window=WIN_FILLLIST;
	filllist_refresh_screen();
	
	return 0;
}

int goto_positionlist_window_from_acclist()
{
	if(vAccounts.empty())
		return 0;
	if(acclist_curr_line==0){	// first select
		acclist_curr_line=1;
		return 0;
	}
	if(vPositions.empty()) {
		positionlist_curr_line = 0;
	}else {
		int i;
		for(i=0;i<vPositions.size();i++){
			if(strcmp(vPositions[i].AccID,vAccounts[acclist_curr_pos+acclist_curr_line-1].AccID)==0 && strcmp(vPositions[i].BrokerID,vAccounts[acclist_curr_pos+acclist_curr_line-1].BrokerID)==0)
				break;
		}
		if(i==vPositions.size())
			return 0;
		positionlist_curr_line=i+1;
	}

	working_window=WIN_POSITION;
	positionlist_refresh_screen();
	
	return 0;
}

int goto_positionlist_window_from_log()
{
	working_window=WIN_POSITION;
	positionlist_refresh_screen();
	
	return 0;
}


int column_settings_move_up_1_line()
{
	int i;
	
	if(vcolumns.size()<=2)
		return 0;
	if(column_settings_curr_line==0){	// first select
		column_settings_curr_line=1;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(column_settings_curr_pos+column_settings_curr_line-1<2)
		return 0;
	if(column_settings_curr_line==3 && column_settings_curr_pos==0)	// Already top
		return 0;
	std::vector<COL_ITEM>::iterator iter;
	for(iter=vcolumns.begin(),i=0;iter!=vcolumns.end();iter++,i++){
		if(i==column_settings_curr_pos+column_settings_curr_line-1)
			break;
	}
	auto h =*iter;
	auto l =*(iter-1);
	vcolumns.erase(iter);
	for(iter=vcolumns.begin();iter!=vcolumns.end();iter++){
		if(*iter==l)
			break;
	}
	vcolumns.insert(iter,h);
	display_column(h);
	display_column(l);
	column_settings_move_backward_1_line();
	
	return 0;
}

int column_settings_move_down_1_line()
{

	int i;
	
	if(vcolumns.size()<=2)
		return 0;
	if(column_settings_curr_line==0){	// first select
		column_settings_curr_line=1;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(column_settings_curr_pos+column_settings_curr_line-1<2)
		return 0;
	if(column_settings_curr_line==vcolumns.size()-column_settings_curr_pos)	// Already bottom
		return 0;
	std::vector<COL_ITEM>::iterator iter;
	for(iter=vcolumns.begin(),i=0;iter!=vcolumns.end();iter++,i++){
		if(i==column_settings_curr_pos+column_settings_curr_line-1)
			break;
	}
	auto h =*iter;
	auto l =*(iter-1);
	vcolumns.erase(iter+1);
	for(iter=vcolumns.begin();iter!=vcolumns.end();iter++){
		if(*iter==l)
			break;
	}
	vcolumns.insert(iter,h);
	display_column(h);
	display_column(l);
	column_settings_move_forward_1_line();
	
	return 0;
}

int column_settings_select_column()
{
	std::vector<COL_ITEM>::iterator iter;
	int i;
	
	if(vcolumns.size()<=2)
		return 0;
	if(column_settings_curr_line==0){	// first select
		column_settings_curr_line=1;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(column_settings_curr_pos+column_settings_curr_line-1<1)
		return 0;
	for(iter=vcolumns.begin(),i=0;iter!=vcolumns.end();iter++,i++){
		if(i==column_settings_curr_pos+column_settings_curr_line-1)
			break;
	}
	mcolumns[*iter]=!mcolumns[*iter];
	display_column(*iter);
	mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);

	return 0;
}

int column_settings_move_backward_1_line(){
	int i;
	if(vcolumns.size()<=2)
		return 0;
	if(column_settings_curr_line==0){	// first select
		column_settings_curr_line=1;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(column_settings_curr_line==1 && column_settings_curr_pos==0)	// Already top
		return 0;
	if(column_settings_curr_line>1){
		mvchgat(column_settings_curr_line,0,-1,A_NORMAL,0,NULL);
		column_settings_curr_line--;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(column_settings_curr_line,0,-1,A_NORMAL,0,NULL);
		move(1,0);
		setscrreg(1,column_settings_max_lines);
		scrl(-1);
		setscrreg(0,column_settings_max_lines+1);
		column_settings_curr_pos--;
		std::vector<COL_ITEM>::iterator iter;
		for(iter=vcolumns.begin(),i=0;iter!=vcolumns.end();iter++,i++){
			if(i==column_settings_curr_pos)
				break;
		}
		display_column(*iter);	// new line
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
	}
	
	return 0;
}

int column_settings_move_forward_1_line()
{

	int i;

	if(vcolumns.size()<=2)
		return 0;
	if(column_settings_curr_line==0){	// first select
		column_settings_curr_line=1;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(column_settings_curr_line==vcolumns.size()-column_settings_curr_pos)	// Already bottom
		return 0;
	if(column_settings_curr_line!=column_settings_max_lines){
		mvchgat(column_settings_curr_line,0,-1,A_NORMAL,0,NULL);
		column_settings_curr_line++;
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(column_settings_curr_line,0,-1,A_NORMAL,0,NULL);
		move(1,0);
		setscrreg(1,column_settings_max_lines);
		scroll(stdscr);
		setscrreg(0,column_settings_max_lines+1);
		column_settings_curr_pos++;
		std::vector<COL_ITEM>::iterator iter;
		for(iter=vcolumns.begin(),i=0;iter!=vcolumns.end();iter++,i++){
			if(i==column_settings_curr_pos+column_settings_max_lines-1)
				break;
		}
		display_column(*iter);	// new line
		mvchgat(column_settings_curr_line,0,-1,A_REVERSE,0,NULL);
	}
	
	return 0;
}

int input_parse_symbol(int *num,int *cmd)
{
	const char* p;
	char strnum[10];
	int i;
	int len;
	
	memset(strnum,0x00,sizeof(strnum));
	for(p=input_buffer,i=0;isdigit(*(unsigned char*)p);++p,i++)
		strnum[i]=*p;
	*num=atol(strnum);
	switch(*p){
	case '\0':	// uncompleted
		return -1;
	case ':':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case '/':	// not supported
		++p;
		len=strlen(p);
		memmove(input_buffer,p,len);
		input_buffer[len]='\0';
		return -1;
	case 'g':
		++p;
		switch(*p){
		case '\0':
			return -1;	// uncompleted
		case 'g':
			*cmd='g';
			break;
		case 't':
			*cmd=input_buffer[0];
			break;
		default:
			++p;
			len=strlen(p);
			memmove(input_buffer,p,len);
			input_buffer[len]='\0';
			return -1;
		}
		break;
		default:
			*cmd=*p;
			break;
	}
	++p;
	len=strlen(p);
	memmove(input_buffer,p,len);
	input_buffer[len]='\0';
	
	return 0;
}

int on_key_pressed_symbol(int ch)
{
	int Num,Cmd;
	
	input_buffer[strlen(input_buffer)+1]='\0';
	input_buffer[strlen(input_buffer)]=ch;
	if(input_parse(&Num,&Cmd)<0)
		return 0;
	
	if(Cmd=='q'){
		return -1;
	}
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
		goto_mainboard_window_from_symbol();
		return 0;
	case 32:	// Space
		goto_mainboard_window_from_symbol();
		break;
	case KEYBOARD_ENTER:	// Enter
		goto_mainboard_window_from_symbol();
		return 0;
	case KEYBOARD_REFRESH:		// ^L
		symbol_refresh_screen();
		break;
	default:
		break;
	}
	symbol_display_status();
	return 0;
}

void CTradeRsp::HandleFrontConnected()
{
	status_print("交易通道已连接");
	TradeConnectionStatus=CONNECTION_STATUS_CONNECTED;
	display_status();

	m_nTradeRequestID=0;

	if(strlen(UserProductInfo)){
		CThostFtdcReqAuthenticateField AuthenticateReq{};
		memset(&AuthenticateReq,0x00,sizeof(AuthenticateReq));
		strncpy(AuthenticateReq.BrokerID,broker,sizeof(AuthenticateReq.BrokerID)-1);
		strncpy(AuthenticateReq.UserID,user,sizeof(AuthenticateReq.UserID)-1);
		strncpy(AuthenticateReq.UserProductInfo,UserProductInfo,sizeof(AuthenticateReq.UserProductInfo)-1);
		strncpy(AuthenticateReq.AppID,AppID,sizeof(AuthenticateReq.AppID)-1);
		strcpy(AuthenticateReq.AuthCode,AuthCode); // XTP的认证Key超长，需要借用到后一字段（AppID）的空间
		m_pTradeReq->ReqAuthenticate(&AuthenticateReq,m_nTradeRequestID++);
	}else{
		CThostFtdcReqUserLoginField Req{};
	
		memset(&Req,0x00,sizeof(Req));
		strcpy(Req.BrokerID,broker);
		strcpy(Req.UserID,user);
		strcpy(Req.Password,passwd);
		sprintf(Req.UserProductInfo,"%s",UserProductInfo);
		m_pTradeReq->ReqUserLogin(&Req,m_nTradeRequestID++);
	}
}
void CTradeRsp::HandleFrontDisconnected(int nReason)
{
	status_print("交易通道已断开");
	TradeConnectionStatus=CONNECTION_STATUS_DISCONNECTED;
	switch(working_window){
	case WIN_MAINBOARD:
		refresh_screen();
		break;
	case WIN_ORDER:
		order_refresh_screen();
		break;
	case WIN_COLUMN_SETTINGS:
		column_settings_refresh_screen();
		break;
	case WIN_SYMBOL:
		symbol_refresh_screen();
		break;
	case WIN_ORDERLIST:
		orderlist_refresh_screen();
		break;
	case WIN_FILLLIST:
		filllist_refresh_screen();
		break;
	case WIN_POSITION:
		positionlist_refresh_screen();
		break;
	case WIN_MONEY:
		acclist_refresh_screen();
		break;
	default:
		break;
	}
}
void CTradeRsp::HandleRspAuthenticate(CThostFtdcRspAuthenticateField& RspAuthenticateField, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0)
		status_print("%s终端认证失败:%s", user, RspInfo.ErrorMsg);
	else
		status_print("%s终端认证成功.", user);

	CThostFtdcReqUserLoginField Req{};
	
	memset(&Req,0x00,sizeof(Req));
	strcpy(Req.BrokerID,broker);
	strcpy(Req.UserID,user);
	strcpy(Req.Password,passwd);
	sprintf(Req.UserProductInfo,"%s",UserProductInfo);
	sprintf(Req.ClientIPAddress,"%s",ClientIPAddress);
	sprintf(Req.MacAddress,"%s",MacAddress);
	sprintf(Req.LoginRemark,"%s",LoginRemark);
	m_pTradeReq->ReqUserLogin(&Req,m_nTradeRequestID++);
}

void CTradeRsp::HandleRspUserLogin(CThostFtdcRspUserLoginField& RspUserLogin,CThostFtdcRspInfoField& RspInfo,int nRequestID,bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("%s登录失败:%s",user,RspInfo.ErrorMsg);
		TradeConnectionStatus=CONNECTION_STATUS_LOGINFAILED;
		display_status();
		return;
	}
	status_print("%s登录成功.",user);

	// Clear Order Operations
// 	vInputingOrders.clear();
	std::vector<CThostFtdcInputOrderActionField>::iterator iter;
	for(iter=vCancelingOrders.begin();iter!=vCancelingOrders.end();){
		if(strcmp(iter->InvestorID,user)==0){
			vCancelingOrders.erase(iter);
			iter=vCancelingOrders.begin();
			continue;
		}else{
			iter++;
		}
	}

	// reset windows on new TradingDay.
	if (strcmp(RspUserLogin.TradingDay, tradedate) != 0) {
		orderlist_reset(user);
		filllist_reset(user);
		positionlist_reset(user);
		acclist_reset(user);
	}

	TradeConnectionStatus=CONNECTION_STATUS_LOGINOK;
	TradeFrontID=RspUserLogin.FrontID;
	TradeSessionID=RspUserLogin.SessionID;
	TradeOrderRef=atol(RspUserLogin.MaxOrderRef);
	sprintf(tradedate,"%4.4s-%2.2s-%2.2s",RspUserLogin.TradingDay,RspUserLogin.TradingDay+4,RspUserLogin.TradingDay+6);
	display_status();

	// 确认结算单
	CThostFtdcSettlementInfoConfirmField SettlementInfoConfirmField;
	
	memset(&SettlementInfoConfirmField,0x00,sizeof(SettlementInfoConfirmField));
	strncpy(SettlementInfoConfirmField.BrokerID,broker,sizeof(SettlementInfoConfirmField.BrokerID)-1);
	strncpy(SettlementInfoConfirmField.InvestorID,user,sizeof(SettlementInfoConfirmField.InvestorID)-1);
	strncpy(SettlementInfoConfirmField.ConfirmDate,RspUserLogin.TradingDay,sizeof(SettlementInfoConfirmField.ConfirmDate)-1);
	strncpy(SettlementInfoConfirmField.ConfirmTime,RspUserLogin.LoginTime,sizeof(SettlementInfoConfirmField.ConfirmTime)-1);
	m_pTradeReq->ReqSettlementInfoConfirm(&SettlementInfoConfirmField,m_nTradeRequestID++);

	CThostFtdcQryInstrumentField Req;
	int r=0;

	memset(&Req,0x00,sizeof(Req));
	while((r= m_pTradeReq->ReqQryInstrument(&Req,m_nTradeRequestID++))==-2 || r==-3)
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}
void CTradeRsp::HandleRspUserLogout(CThostFtdcUserLogoutField& UserLogout,CThostFtdcRspInfoField& RspInfo,int nRequestID,bool bIsLast)
{

}
void CTradeRsp::HandleRspQryInstrument(CThostFtdcInstrumentField& Instrument, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	int index;

	if(RspInfo.ErrorID!=0){
		status_print("查询合约失败:%s",RspInfo.ErrorMsg);
		return;
	}

	// Subscribe
	auto iter = mInstrumentIndex.find(Instrument.InstrumentID);
	if (iter != mInstrumentIndex.end()) {
		index = iter->second;
		if (index<curr_pos || index>curr_pos + max_lines - 1 || vquotes[index].subscribed)
			return;
		subscribe(index);
		return;
	}

	if(Instrument.InstrumentID[0]!='\0'){
		quotation_t quote;
		memset(&quote,0x00,sizeof(quote));
		strcpy(quote.product_id,Instrument.InstrumentID);
		strcpy(quote.exchange_id,Instrument.ExchangeID);
		if(strlen(Instrument.InstrumentName))
			strcpy(quote.product_name,Instrument.InstrumentName);
		else
			strcpy(quote.product_name, Instrument.InstrumentID);
		if(Instrument.PriceTick>=1)
			quote.precision=0;
		else if(Instrument.PriceTick>=0.1)
			quote.precision=1;
		else if(Instrument.PriceTick>=0.01)
			quote.precision=2;
		else if(Instrument.PriceTick>=0.001)
			quote.precision=3;
		else if(Instrument.PriceTick>=0.0001)
			quote.precision=4;
		else if(Instrument.PriceTick>=0.00001)
			quote.precision=5;
		else
			quote.precision=6;
		memcpy(&quote.Instrument, &Instrument, sizeof(Instrument));
	
		index = vquotes.size();
		mInstrumentIndex[Instrument.InstrumentID] = index;
		vquotes.push_back(quote);
		
		display_quotation(index);
		if(vquotes.size()-1>=curr_pos && vquotes.size()-1<=curr_pos+max_lines-1)
			subscribe(index);
	}

	if(vquotes.empty() || !bIsLast)
		return;
	status_print("查询合约成功.");
	
	CThostFtdcQryInvestorPositionField Req;
	int r = 0;

	memset(&Req, 0x00, sizeof(Req));
	strcpy(Req.BrokerID, broker);
	strcpy(Req.InvestorID, user);
	while ((r = m_pTradeReq->ReqQryInvestorPosition(&Req, m_nTradeRequestID++)) == -2 || r == -3)
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

void CTradeRsp::HandleRspQryOrder(CThostFtdcOrderField& Order, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("查询订单失败:%s", RspInfo.ErrorMsg);
		return;
	}
	std::vector<CThostFtdcOrderField>::iterator iter;
	if(strlen(Order.InstrumentID)!=0){
		for(iter=vOrders.begin();iter!=vOrders.end();iter++){
			if(Order.FrontID==iter->FrontID && Order.SessionID==iter->SessionID && strcmp(Order.OrderRef,iter->OrderRef)==0){
				memcpy(iter->BrokerID,&Order,sizeof(CThostFtdcOrderField));
				break;
			}
		}
		if(iter==vOrders.end())
			vOrders.push_back(Order);
		switch(working_window){
		case WIN_ORDERLIST:
			orderlist_redraw();
			break;
		default:
			break;
		}
	}
	
	if(!bIsLast)
		return;
	status_print("查询订单成功.");
	CThostFtdcQryTradeField Req;
	int r=0;
	
	memset(&Req,0x00,sizeof(Req));
	strcpy(Req.BrokerID,broker);
	strcpy(Req.InvestorID,user);
	while((r=m_pTradeReq->ReqQryTrade(&Req,m_nTradeRequestID++))==-2 || r==-3)
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}
void CTradeRsp::HandleRspQryTrade(CThostFtdcTradeField& Trade, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("查询成交单失败:%s",RspInfo.ErrorMsg);
		return;
	}
	std::vector<CThostFtdcTradeField>::iterator iter;
	if(strlen(Trade.InstrumentID)!=0){
		for(iter=vFilledOrders.begin();iter!=vFilledOrders.end();iter++){
			if(strcmp(Trade.ExchangeID,iter->ExchangeID)==0 && strcmp(Trade.OrderSysID,iter->OrderSysID)==0){
				memcpy(iter->BrokerID,&Trade,sizeof(CThostFtdcTradeField));
				break;
			}
		}
		if(iter==vFilledOrders.end())
			vFilledOrders.push_back(Trade);
		switch(working_window){
		case WIN_FILLLIST:
			filllist_redraw();
			break;
		default:
			break;
		}
	}
	
	if(!bIsLast)
		return;
	status_print("查询成交单成功.");
}

void CTradeRsp::HandleRspOrderInsert(CThostFtdcInputOrderField& InputOrder, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("报单失败:%s",RspInfo.ErrorMsg);
		std::vector<CThostFtdcOrderField>::iterator iter;
		for(iter=vOrders.begin();iter!=vOrders.end();iter++){
			if(iter->FrontID==TradeFrontID && iter->SessionID==TradeSessionID && strcmp(iter->OrderRef,InputOrder.OrderRef)==0){
				if(iter->OrderStatus==THOST_FTDC_OST_Canceled)	// 如果已经撤消,则不再重复处理
					break;
				std::vector<stPosition_t>::iterator iterPosi;
				for(iterPosi=vPositions.begin();iterPosi!=vPositions.end();iterPosi++){
					if(strcmp(InputOrder.InvestorID,iterPosi->AccID)==0 && strcmp(InputOrder.InstrumentID,iterPosi->InstrumentID)==0)
						break;
				}
				if(iterPosi!=vPositions.end()){  // 本Session中发出的定单肯定会有持仓记录
					//委托失败后释放冻结仓位
					if(InputOrder.Direction==THOST_FTDC_D_Buy){
						if(InputOrder.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(InputOrder.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->SellVolume-iterPosi->TodaySellVolume)==0)
								iterPosi->TodayFrozenSellVolume-=iter->VolumeTotal;
							iterPosi->FrozenSellVolume-=iter->VolumeTotal;
						}
						iterPosi->BuyingVolume-=iter->VolumeTotal;
					}else{
						if(InputOrder.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(InputOrder.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->BuyVolume-iterPosi->TodayBuyVolume)==0)
								iterPosi->TodayFrozenBuyVolume-=iter->VolumeTotal;
							iterPosi->FrozenBuyVolume-=iter->VolumeTotal;
						}
						iterPosi->SellingVolume-=iter->VolumeTotal;
					}
				}
				iter->OrderStatus=THOST_FTDC_OST_Canceled;
				strcpy(iter->StatusMsg,RspInfo.ErrorMsg);
				break;
			}
		}

		switch(working_window){
		case WIN_ORDERLIST:
			orderlist_redraw();
			break;
		case WIN_ORDER:
			order_redraw();
			break;
		case WIN_POSITION:
			positionlist_redraw();
			break;
		default:
			break;
		}
	}
}

void CTradeRsp::HandleRspOrderAction(CThostFtdcInputOrderActionField& InputOrderAction, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("撤单失败:%s",RspInfo.ErrorMsg);
		std::vector<CThostFtdcInputOrderActionField>::iterator iter;
		for(iter=vCancelingOrders.begin();iter!=vCancelingOrders.end();iter++){
			if(iter->FrontID==InputOrderAction.FrontID && iter->SessionID==InputOrderAction.SessionID && strcmp(iter->OrderRef,InputOrderAction.OrderRef)==0){
				vCancelingOrders.erase(iter);	// 移除正在撤消的报单
				// 如果正在改单，则取消操作
				std::vector<CThostFtdcOrderField>::iterator i;
				for(i=m_mMovingOrders.begin();i!=m_mMovingOrders.end();i++){
					if(InputOrderAction.FrontID==i->FrontID && InputOrderAction.SessionID==i->SessionID && strcmp(InputOrderAction.OrderRef,i->OrderRef)==0){
						m_mMovingOrders.erase(i);
						break;
					}
				}
				break;
			}
		}

// 		std::vector<CThostFtdcOrderField>::iterator iterOrder;
// 		for(iterOrder=vOrders.begin();iterOrder!=vOrders.end();iterOrder++){
// 			if(strcmp(iterOrder->InstrumentID,pInputOrderAction->InstrumentID)==0 && iterOrder->FrontID==pInputOrderAction->FrontID && iterOrder->SessionID==pInputOrderAction->SessionID && strcmp(iterOrder->OrderRef,pInputOrderAction->OrderRef)==0){
// 				iterOrder->OrderStatus=THOST_FTDC_OST_Canceled;	// 更新报单状态
// 				break;
// 			}
// 		}
		switch(working_window){
		case WIN_ORDERLIST:
			orderlist_redraw();
			break;
		case WIN_ORDER:
			order_redraw();
			break;
		case WIN_POSITION:
			positionlist_redraw();
			break;
		default:
			break;
		}
	}
}

void CTradeRsp::HandleRspQryInvestorPosition(CThostFtdcInvestorPositionField& InvestorPosition, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("查询持仓失败:%s", RspInfo.ErrorMsg);
		return;
	}
	if(strlen(InvestorPosition.InstrumentID)>0 && InvestorPosition.HedgeFlag == THOST_FTDC_HF_Speculation)
		vInvestorPositions.push_back(InvestorPosition);
	if(!bIsLast)
		return;
	status_print("查询持仓成功.");

	// 清空持仓
	positionlist_reset(user);

	// 通过持仓信息取得昨仓
	std::vector<CThostFtdcInvestorPositionField>::iterator iterInvestorPosition;
	std::vector<stPosition_t>::iterator iter;
	for(iterInvestorPosition=vInvestorPositions.begin();iterInvestorPosition!=vInvestorPositions.end();iterInvestorPosition++){
		if(strcmp(iterInvestorPosition->InvestorID,user)!=0)
			continue;
		for(iter=vPositions.begin();iter!=vPositions.end();iter++){
			if(strcmp(iterInvestorPosition->InvestorID,iter->AccID)==0 && strcmp(iterInvestorPosition->InstrumentID,iter->InstrumentID)==0){
				if(iterInvestorPosition->PosiDirection==THOST_FTDC_PD_Long){
					iter->AvgBuyPrice = iterInvestorPosition->PreSettlementPrice;
					iter->BuyVolume+=iterInvestorPosition->YdPosition;
					iter->Volume+=iterInvestorPosition->YdPosition;
				}else{
					iter->AvgSellPrice = iterInvestorPosition->PreSettlementPrice;
					iter->SellVolume+=iterInvestorPosition->YdPosition;
					iter->Volume-=iterInvestorPosition->YdPosition;
				}
				iter->Price = iterInvestorPosition->PreSettlementPrice;
				break;
			}
		}
		if(iter==vPositions.end()){
			stPosition_t Posi;
			memset(&Posi,0x00,sizeof(Posi));
			strcpy(Posi.InstrumentID,iterInvestorPosition->InstrumentID);
			strcpy(Posi.BrokerID,iterInvestorPosition->BrokerID);
			strcpy(Posi.AccID,iterInvestorPosition->InvestorID);
			for(auto & vquote : vquotes){
				if(strcmp(Posi.InstrumentID,vquote.product_id)==0){
					strcpy(Posi.ExchangeID,vquote.exchange_id);
					break;
				}
			}

			if(iterInvestorPosition->PosiDirection==THOST_FTDC_PD_Long){
				Posi.AvgBuyPrice = iterInvestorPosition->PreSettlementPrice;
				Posi.BuyVolume+=iterInvestorPosition->YdPosition;
				Posi.Volume+=iterInvestorPosition->YdPosition;
			}else{
				Posi.AvgSellPrice = iterInvestorPosition->PreSettlementPrice;
				Posi.SellVolume+=iterInvestorPosition->YdPosition;
				Posi.Volume-=iterInvestorPosition->YdPosition;
			}
			Posi.Price = iterInvestorPosition->PreSettlementPrice;
			mPositionIndex[Posi.InstrumentID] = vPositions.size();
			vPositions.push_back(Posi);
		}
	}

	// 删除vInvestorPositions中相应投资者的持仓信息
	for(iterInvestorPosition=vInvestorPositions.begin();iterInvestorPosition!=vInvestorPositions.end();){
		if(strcmp(iterInvestorPosition->InvestorID,user)==0){
			vInvestorPositions.erase(iterInvestorPosition);
			iterInvestorPosition=vInvestorPositions.begin();
			continue;
		}else{
			iterInvestorPosition++;
		}
	}
	

	// 通过成交明细更新持仓
	std::vector<CThostFtdcTradeField>::iterator iterTrade;
	for(iterTrade=vFilledOrders.begin();iterTrade!=vFilledOrders.end();iterTrade++){
		if(strcmp(iterTrade->InvestorID,user)!=0)
			continue;
		for(iter=vPositions.begin();iter!=vPositions.end();iter++){
			if(strcmp(iterTrade->InvestorID,iter->AccID)==0 && strcmp(iter->InstrumentID,iterTrade->InstrumentID)==0){
				if(iterTrade->Direction==THOST_FTDC_D_Buy){
					if(iterTrade->OffsetFlag==THOST_FTDC_OF_Open){
						iter->AvgBuyPrice = (iter->AvgBuyPrice * iter->BuyVolume + iterTrade->Price * iterTrade->Volume) / (iter->BuyVolume + iterTrade->Volume);
						iter->BuyVolume+=iterTrade->Volume;
						iter->TodayBuyVolume+=iterTrade->Volume;
					}else{
						if(iterTrade->OffsetFlag==THOST_FTDC_OF_CloseToday || (iter->SellVolume-iter->TodaySellVolume)==0)
							iter->TodaySellVolume-=iterTrade->Volume;
						iter->SellVolume-=iterTrade->Volume;
						if (iter->SellVolume == 0)
							iter->AvgSellPrice = 0;
					}
					iter->Volume+=iterTrade->Volume;
				}else{
					if(iterTrade->OffsetFlag==THOST_FTDC_OF_Open){
						iter->AvgSellPrice = (iter->AvgSellPrice * iter->SellVolume + iterTrade->Price * iterTrade->Volume) / (iter->SellVolume + iterTrade->Volume);
						iter->SellVolume+=iterTrade->Volume;
						iter->TodaySellVolume+=iterTrade->Volume;
					}else{
						if(iterTrade->OffsetFlag==THOST_FTDC_OF_CloseToday || (iter->BuyVolume-iter->TodayBuyVolume)==0)
							iter->TodayBuyVolume-=iterTrade->Volume;
						iter->BuyVolume-=iterTrade->Volume;
						if (iter->BuyVolume == 0)
							iter->AvgBuyPrice = 0;
					}
					iter->Volume-=iterTrade->Volume;
				}
				if (iter->BuyVolume > iter->SellVolume)
					iter->Price = iter->AvgBuyPrice;
				else
					iter->Price = iter->AvgSellPrice;
				break;
			}
		}
		if(iter==vPositions.end()){
			stPosition_t Posi;
			memset(&Posi,0x00,sizeof(Posi));
			strcpy(Posi.InstrumentID,iterTrade->InstrumentID);
			strcpy(Posi.BrokerID,iterTrade->BrokerID);
			strcpy(Posi.AccID,iterTrade->InvestorID);
			strcpy(Posi.ExchangeID,iterTrade->ExchangeID);
			if(iterTrade->Direction==THOST_FTDC_D_Buy){
				if(iterTrade->OffsetFlag==THOST_FTDC_OF_Open){
					Posi.AvgBuyPrice = (Posi.AvgBuyPrice * Posi.BuyVolume + iterTrade->Price * iterTrade->Volume) / (Posi.BuyVolume + iterTrade->Volume);
					Posi.BuyVolume+=iterTrade->Volume;
					Posi.TodayBuyVolume+=iterTrade->Volume;
				}else{
					if(iterTrade->OffsetFlag==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
						Posi.TodaySellVolume-=iterTrade->Volume;
					Posi.SellVolume-=iterTrade->Volume;
					if(Posi.SellVolume == 0)
						Posi.AvgSellPrice = 0;
				}
				Posi.Volume+=iterTrade->Volume;
			}else{
				if(iterTrade->OffsetFlag==THOST_FTDC_OF_Open){
					Posi.AvgSellPrice = (Posi.AvgSellPrice * Posi.SellVolume + iterTrade->Price * iterTrade->Volume) / (Posi.SellVolume + iterTrade->Volume);
					Posi.SellVolume+=iterTrade->Volume;
					Posi.TodaySellVolume+=iterTrade->Volume;
				}else{
					if(iterTrade->OffsetFlag==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
						Posi.TodayBuyVolume-=iterTrade->Volume;
					Posi.BuyVolume-=iterTrade->Volume;
					if (Posi.BuyVolume == 0)
						Posi.AvgBuyPrice = 0;
				}
				Posi.Volume-=iterTrade->Volume;
			}
			if (Posi.BuyVolume > Posi.SellVolume)
				Posi.Price = Posi.AvgBuyPrice;
			else
				Posi.Price = Posi.AvgSellPrice;
			mPositionIndex[Posi.InstrumentID] = vPositions.size();
			vPositions.push_back(Posi);
		}
	}


	// 通过委托明细冻结持仓
	std::vector<CThostFtdcOrderField>::iterator iterOrder;
	for(iterOrder=vOrders.begin();iterOrder!=vOrders.end();iterOrder++){
		if(strcmp(iterOrder->InvestorID,user)!=0)
			continue;
		if(iterOrder->OrderStatus!=THOST_FTDC_OST_Canceled && iterOrder->OrderStatus!=THOST_FTDC_OST_AllTraded){
			std::vector<stPosition_t>::iterator iterPosi;
			for(iterPosi=vPositions.begin();iterPosi!=vPositions.end();iterPosi++){
				if(strcmp(iterOrder->InvestorID,iterPosi->AccID)==0 && strcmp(iterOrder->InstrumentID,iterPosi->InstrumentID)==0)
					break;
			}
			if(iterPosi!=vPositions.end()){
				switch(iterOrder->OrderStatus){
				case THOST_FTDC_OST_PartTradedQueueing:	//部分成交冻结相应的仓位
					if(iterOrder->Direction==THOST_FTDC_D_Buy){
						if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->SellVolume-iterPosi->TodaySellVolume)==0)
								iterPosi->TodayFrozenSellVolume+=iterOrder->VolumeTotal;
							iterPosi->FrozenSellVolume+=iterOrder->VolumeTotal;
						}
						iterPosi->BuyingVolume+=iterOrder->VolumeTotal;
					}else{
						if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->BuyVolume-iterPosi->TodayBuyVolume)==0)
								iterPosi->TodayFrozenBuyVolume+=iterOrder->VolumeTotal;
							iterPosi->FrozenBuyVolume+=iterOrder->VolumeTotal;
						}
						iterPosi->SellingVolume+=iterOrder->VolumeTotal;
					}
					break;
				default:	// 未成交冻结仓位
					if(iterOrder->Direction==THOST_FTDC_D_Buy){
						if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->SellVolume-iterPosi->TodaySellVolume)==0)
								iterPosi->TodayFrozenSellVolume+=iterOrder->VolumeTotal;
							iterPosi->FrozenSellVolume+=iterOrder->VolumeTotal;
						}
						iterPosi->BuyingVolume+=iterOrder->VolumeTotal;
					}else{
						if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->BuyVolume-iterPosi->TodayBuyVolume)==0)
								iterPosi->TodayFrozenBuyVolume+=iterOrder->VolumeTotal;
							iterPosi->FrozenBuyVolume+=iterOrder->VolumeTotal;
						}
						iterPosi->SellingVolume+=iterOrder->VolumeTotal;
					}
					break;
				}
			}else{
				if(iterOrder->OrderStatus!=THOST_FTDC_OST_Canceled && iterOrder->OrderStatus!=THOST_FTDC_OST_AllTraded){ // 如果是新定单，且已撤消或已全部成交，就不会影响处理冻结仓位
					stPosition_t Posi;
					memset(&Posi,0x00,sizeof(Posi));
					strcpy(Posi.InstrumentID,iterOrder->InstrumentID);
					strcpy(Posi.BrokerID,iterOrder->BrokerID);
					strcpy(Posi.AccID,iterOrder->InvestorID);
					strcpy(Posi.ExchangeID,iterOrder->ExchangeID);
					switch(iterOrder->OrderStatus){
					case THOST_FTDC_OST_PartTradedQueueing:	//部分成交冻结相应的仓位
						if(iterOrder->Direction==THOST_FTDC_D_Buy){
							if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
									Posi.TodayFrozenSellVolume+=iterOrder->VolumeTotal;
								Posi.FrozenSellVolume+=iterOrder->VolumeTotal;
							}
							Posi.BuyingVolume+=iterOrder->VolumeTotal;
						}else{
							if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
									Posi.TodayFrozenBuyVolume+=iterOrder->VolumeTotal;
								Posi.FrozenBuyVolume+=iterOrder->VolumeTotal;
							}
							Posi.SellingVolume+=iterOrder->VolumeTotal;
						}
						break;
					default:	// 未成交冻结仓位
						if(iterOrder->Direction==THOST_FTDC_D_Buy){
							if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
									Posi.TodayFrozenSellVolume+=iterOrder->VolumeTotal;
								Posi.FrozenSellVolume+=iterOrder->VolumeTotal;
							}
							Posi.BuyingVolume+=iterOrder->VolumeTotal;
						}else{
							if(iterOrder->CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(iterOrder->CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
									Posi.TodayFrozenBuyVolume+=iterOrder->VolumeTotal;
								Posi.FrozenBuyVolume+=iterOrder->VolumeTotal;
							}
							Posi.SellingVolume+=iterOrder->VolumeTotal;
						}
						break;
					}
					mPositionIndex[Posi.InstrumentID] = vPositions.size();
					vPositions.push_back(Posi);
				}
			}
		}
	}

	// 刷新窗口
	switch(working_window){
	case WIN_ORDER:
		order_redraw();
		break;
	case WIN_POSITION:
		positionlist_redraw();
		break;
	default:
		break;
	}

	// 持仓处理完毕,查询资金
}

void CTradeRsp::HandleRspQryTradingAccount(CThostFtdcTradingAccountField& TradingAccount, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("查询资金失败:%s", RspInfo.ErrorMsg);
		return;
	}
	//status_print("查询资金成功.");

	std::vector<stAccount_t>::iterator iter;
	for(iter=vAccounts.begin();iter!=vAccounts.end();iter++){
		if(strcmp(TradingAccount.AccountID,iter->AccID)==0){
			iter->PreBalance=TradingAccount.PreBalance;
			iter->MoneyIn=TradingAccount.Deposit;
			iter->MoneyOut=TradingAccount.Withdraw;
			iter->FrozenMargin=TradingAccount.FrozenMargin;
			iter->MoneyFrozen=TradingAccount.FrozenCash;
			iter->FeeFrozen=TradingAccount.FrozenCommission;
			iter->Margin=TradingAccount.CurrMargin;
			iter->Fee=TradingAccount.Commission;
			iter->CloseProfitLoss=TradingAccount.CloseProfit;
			iter->FloatProfitLoss=TradingAccount.PositionProfit;
			iter->BalanceAvailable=TradingAccount.Available;
			break;
		}
	}
	switch(working_window){
	case WIN_MONEY:
		acclist_display_acc(TradingAccount.BrokerID,TradingAccount.AccountID);
		acclist_redraw();
		break;
	default:
		break;
	}
}


void CTradeRsp::HandleRtnOrder(CThostFtdcOrderField& Order)
{
	char action[10];
	if (Order.Direction == THOST_FTDC_D_Buy && Order.CombOffsetFlag[0] == THOST_FTDC_OF_Open)
		strcpy(action, "买开");
	else if (Order.Direction == THOST_FTDC_D_Buy && Order.CombOffsetFlag[0] == THOST_FTDC_OF_CloseToday)
		strcpy(action, "买平今");
	else if (Order.Direction == THOST_FTDC_D_Sell && Order.CombOffsetFlag[0] == THOST_FTDC_OF_Open)
		strcpy(action, "卖开");
	else if (Order.Direction == THOST_FTDC_D_Sell && Order.CombOffsetFlag[0] == THOST_FTDC_OF_CloseToday)
		strcpy(action, "卖平今");
	else if (Order.Direction == THOST_FTDC_D_Buy)
		strcpy(action, "买平");
	else
		strcpy(action, "卖平");

	char order_status[20];
	if (Order.OrderStatus == THOST_FTDC_OST_AllTraded)
		strcpy(order_status, "全部成交");
	else if (Order.OrderStatus == THOST_FTDC_OST_Canceled)
		strcpy(order_status, "已撤消");
	else if (Order.OrderStatus == THOST_FTDC_OST_Unknown)
		strcpy(order_status, "申报中");
	else if (Order.OrderStatus == THOST_FTDC_OST_NoTradeQueueing)
		strcpy(order_status, "已报入");
	else if (Order.OrderStatus == THOST_FTDC_OST_PartTradedQueueing)
		strcpy(order_status, "部分成交");
	else
		strcpy(order_status, "未知");
	status_print( "%s %.2f %s 剩余%d手 %s. %s", Order.InstrumentID, Order.LimitPrice, action, Order.VolumeTotalOriginal-Order.VolumeTraded, order_status, Order.StatusMsg);

	std::vector<CThostFtdcOrderField>::iterator iter;
	std::vector<stPosition_t>::iterator iterPosi;
	std::vector<CThostFtdcInputOrderActionField>::iterator iterCancelingOrder;
	bool bExists=false;
	if(Order.InstrumentID[0]!='\0'){
		for(iter=vOrders.begin();iter!=vOrders.end();iter++){
			if(Order.FrontID==iter->FrontID && Order.SessionID==iter->SessionID && strcmp(Order.OrderRef,iter->OrderRef)==0){
				if(iter->OrderStatus!=THOST_FTDC_OST_Canceled && iter->OrderStatus!=THOST_FTDC_OST_AllTraded){
					for(iterPosi=vPositions.begin();iterPosi!=vPositions.end();iterPosi++){
						if(strcmp(Order.InvestorID,iterPosi->AccID)==0 && strcmp(Order.InstrumentID,iterPosi->InstrumentID)==0)
							break;
					}
					if(iterPosi!=vPositions.end()){
						switch(Order.OrderStatus){
						case THOST_FTDC_OST_AllTraded:	//成交后释放冻结仓位
						case THOST_FTDC_OST_Canceled:	//撤消后释放冻结仓位
							if(Order.Direction==THOST_FTDC_D_Buy){
								if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
									if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->SellVolume-iterPosi->TodaySellVolume)==0)
										iterPosi->TodayFrozenSellVolume-=iter->VolumeTotal;
									iterPosi->FrozenSellVolume-=iter->VolumeTotal;
								}
								iterPosi->BuyingVolume-=iter->VolumeTotal;
							}else{
								if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
									if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->BuyVolume-iterPosi->TodayBuyVolume)==0)
										iterPosi->TodayFrozenBuyVolume-=iter->VolumeTotal;
									iterPosi->FrozenBuyVolume-=iter->VolumeTotal;
								}
								iterPosi->SellingVolume-=iter->VolumeTotal;
							}
							for(iterCancelingOrder=vCancelingOrders.begin();iterCancelingOrder!=vCancelingOrders.end();iterCancelingOrder++){
								if(strcmp(iterCancelingOrder->InstrumentID,iter->InstrumentID)==0 && iterCancelingOrder->FrontID==iter->FrontID && iterCancelingOrder->SessionID==iter->SessionID && strcmp(iterCancelingOrder->OrderRef,iter->OrderRef)==0){
									vCancelingOrders.erase(iterCancelingOrder);	// 移除正在撤消的报单
									break;
								}
							}
							memcpy(iter->BrokerID,&Order,sizeof(CThostFtdcOrderField));
							if(Order.OrderStatus==THOST_FTDC_OST_Canceled){
								// 改单，如果撤消成功，则报入新订单
								std::vector<CThostFtdcOrderField>::iterator i;
								for(i=m_mMovingOrders.begin();i!=m_mMovingOrders.end();i++){
									if(Order.FrontID==i->FrontID && Order.SessionID==i->SessionID && strcmp(Order.OrderRef,i->OrderRef)==0){
										OrderInsert(Order.InstrumentID,Order.Direction,Order.CombOffsetFlag[0],i->LimitPrice,Order.VolumeTotal);
										m_mMovingOrders.erase(i);
										break;
									}
								}
							}						
							break;
						case THOST_FTDC_OST_PartTradedQueueing:	//部分成交释放相应的冻结仓位
							if(Order.Direction==THOST_FTDC_D_Buy){
								if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
									if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->SellVolume-iterPosi->TodaySellVolume)==0)
										iterPosi->TodayFrozenSellVolume-=iter->VolumeTotal-Order.VolumeTotal;
									iterPosi->FrozenSellVolume-=iter->VolumeTotal-Order.VolumeTotal;
								}
								iterPosi->BuyingVolume-=iter->VolumeTotal-Order.VolumeTotal;
							}else{
								if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
									if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->BuyVolume-iterPosi->TodayBuyVolume)==0)
										iterPosi->TodayFrozenBuyVolume-=iter->VolumeTotal-Order.VolumeTotal;
									iterPosi->FrozenBuyVolume-=iter->VolumeTotal-Order.VolumeTotal;
								}
								iterPosi->SellingVolume-=iter->VolumeTotal-Order.VolumeTotal;
							}
							memcpy(iter->BrokerID,&Order,sizeof(CThostFtdcOrderField));
							break;
						default:
							memcpy(iter->BrokerID,&Order,sizeof(CThostFtdcOrderField));
							break;
						}
					}else{
						memcpy(iter->BrokerID,&Order,sizeof(CThostFtdcOrderField));
					}
				}else{
					memcpy(iter->BrokerID,&Order,sizeof(CThostFtdcOrderField));
				}
				bExists=true;
				break;
			}
		}
		if(!bExists){
			if(Order.OrderStatus!=THOST_FTDC_OST_Canceled && Order.OrderStatus!=THOST_FTDC_OST_AllTraded){ // 如果是新定单，且已撤消或已全部成交，就不会影响处理冻结仓位
				auto iterIndex = mPositionIndex.find(Order.InstrumentID);
				if (iterIndex != mPositionIndex.end()) {
					auto& Posi = vPositions[iterIndex->second];
					switch(Order.OrderStatus){
					case THOST_FTDC_OST_PartTradedQueueing:	//部分成交冻结相应的仓位
						if(Order.Direction==THOST_FTDC_D_Buy){
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
									Posi.TodayFrozenSellVolume+=Order.VolumeTotal;
								Posi.FrozenSellVolume+=Order.VolumeTotal;
							}
							Posi.BuyingVolume+=Order.VolumeTotal;
						}else{
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
									Posi.TodayFrozenBuyVolume+=Order.VolumeTotal;
								Posi.FrozenBuyVolume+=Order.VolumeTotal;
							}
							Posi.SellingVolume+=Order.VolumeTotal;
						}
						break;
					default:	// 未成交冻结仓位
						if(Order.Direction==THOST_FTDC_D_Buy){
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
									Posi.TodayFrozenSellVolume+=Order.VolumeTotal;
								Posi.FrozenSellVolume+=Order.VolumeTotal;
							}
							Posi.BuyingVolume+=Order.VolumeTotal;
						}else{
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
									Posi.TodayFrozenBuyVolume+=Order.VolumeTotal;
								Posi.FrozenBuyVolume+=Order.VolumeTotal;
							}
							Posi.SellingVolume+=Order.VolumeTotal;
						}
						break;
					}
				}else{
					stPosition_t Posi;
					memset(&Posi,0x00,sizeof(Posi));
					strcpy(Posi.InstrumentID,Order.InstrumentID);
					strcpy(Posi.BrokerID,Order.BrokerID);
					strcpy(Posi.AccID,Order.InvestorID);
					for(auto & vquote : vquotes){
						if(strcmp(Posi.InstrumentID,vquote.product_id)==0){
							strcpy(Posi.ExchangeID,vquote.exchange_id);
							break;
						}
					}
					switch(Order.OrderStatus){
					case THOST_FTDC_OST_PartTradedQueueing:	//部分成交冻结相应的仓位
						if(Order.Direction==THOST_FTDC_D_Buy){
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
									Posi.TodayFrozenSellVolume+=Order.VolumeTotal;
								Posi.FrozenSellVolume+=Order.VolumeTotal;
							}
							Posi.BuyingVolume+=Order.VolumeTotal;
						}else{
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
									Posi.TodayFrozenBuyVolume+=Order.VolumeTotal;
								Posi.FrozenBuyVolume+=Order.VolumeTotal;
							}
							Posi.SellingVolume+=Order.VolumeTotal;
						}
						break;
					default:	// 未成交冻结仓位
						if(Order.Direction==THOST_FTDC_D_Buy){
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
									Posi.TodayFrozenSellVolume+=Order.VolumeTotal;
								Posi.FrozenSellVolume+=Order.VolumeTotal;
							}
							Posi.BuyingVolume+=Order.VolumeTotal;
						}else{
							if(Order.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
								if(Order.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
									Posi.TodayFrozenBuyVolume+=Order.VolumeTotal;
								Posi.FrozenBuyVolume+=Order.VolumeTotal;
							}
							Posi.SellingVolume+=Order.VolumeTotal;
						}
						break;
					}
					mPositionIndex[Order.InstrumentID] = vPositions.size();
					vPositions.push_back(Posi);
				}
			}
			vOrders.push_back(Order);
		}
		switch(working_window){
		case WIN_ORDERLIST:
			orderlist_redraw();
			break;
		case WIN_ORDER:
			order_redraw();
			break;
		case WIN_POSITION:
			positionlist_redraw();
			break;
		default:
			break;
		}
	}
}

void CTradeRsp::HandleRtnTrade(CThostFtdcTradeField& Trade)
{
	char action[10];
	if (Trade.Direction == THOST_FTDC_D_Buy && Trade.OffsetFlag == THOST_FTDC_OF_Open)
		strcpy(action, "买开");
	else if (Trade.Direction == THOST_FTDC_D_Buy && Trade.OffsetFlag == THOST_FTDC_OF_CloseToday)
		strcpy(action, "买平今");
	else if (Trade.Direction == THOST_FTDC_D_Sell && Trade.OffsetFlag == THOST_FTDC_OF_Open)
		strcpy(action, "卖开");
	else if (Trade.Direction == THOST_FTDC_D_Sell && Trade.OffsetFlag == THOST_FTDC_OF_CloseToday)
		strcpy(action, "卖平今");
	else if (Trade.Direction == THOST_FTDC_D_Buy)
		strcpy(action, "买平");
	else
		strcpy(action, "卖平");
	status_print( "%s %.2f %s 成交 %d手", Trade.InstrumentID, Trade.Price, action, Trade.Volume);
	// 	std::vector<CThostFtdcTradeField>::iterator iter;
	if(strlen(Trade.InstrumentID)!=0){
// 		for(iter=vFilledOrders.begin();iter!=vFilledOrders.end();iter++){
// 			if(strcmp(Trade.ExchangeID,iter->ExchangeID)==0 && strcmp(Trade.OrderSysID,iter->OrderSysID)==0){
// 				return; //重复数据
// 			}
// 		}
// 		if(iter==vFilledOrders.end())
		vFilledOrders.push_back(Trade);

		// Update Position Info
		auto iterIndex = mPositionIndex.find(Trade.InstrumentID);
		if (iterIndex != mPositionIndex.end()) {
			auto& Posi = vPositions[iterIndex->second];
			if (Trade.Direction == THOST_FTDC_D_Buy) {
				if (Trade.OffsetFlag == THOST_FTDC_OF_Open) {
					Posi.AvgBuyPrice = (Posi.AvgBuyPrice * Posi.BuyVolume + Trade.Price * Trade.Volume) / (Posi.BuyVolume + Trade.Volume);
					Posi.BuyVolume += Trade.Volume;
					Posi.TodayBuyVolume += Trade.Volume;
				}
				else {
					if (Trade.OffsetFlag == THOST_FTDC_OF_CloseToday || (Posi.SellVolume - Posi.TodaySellVolume) == 0)
						Posi.TodaySellVolume -= Trade.Volume;
					Posi.SellVolume -= Trade.Volume;
					if(Posi.SellVolume==0)
						Posi.AvgSellPrice = 0;
				}
				Posi.Volume += Trade.Volume;
			}
			else {
				if (Trade.OffsetFlag == THOST_FTDC_OF_Open) {
					Posi.AvgSellPrice = (Posi.AvgSellPrice * Posi.SellVolume + Trade.Price * Trade.Volume) / (Posi.SellVolume + Trade.Volume);
					Posi.SellVolume += Trade.Volume;
					Posi.TodaySellVolume += Trade.Volume;
				}
				else {
					if (Trade.OffsetFlag == THOST_FTDC_OF_CloseToday || (Posi.BuyVolume - Posi.TodayBuyVolume) == 0)
						Posi.TodayBuyVolume -= Trade.Volume;
					Posi.BuyVolume -= Trade.Volume;
					if (Posi.BuyVolume == 0)
						Posi.AvgBuyPrice = 0;
				}
				Posi.Volume -= Trade.Volume;
			}
			if (Posi.BuyVolume > Posi.SellVolume)
				Posi.Price = Posi.AvgBuyPrice;
			else
				Posi.Price = Posi.AvgSellPrice;
		} else {
			stPosition_t Posi;
			memset(&Posi,0x00,sizeof(Posi));
			strcpy(Posi.InstrumentID,Trade.InstrumentID);
			strcpy(Posi.BrokerID,Trade.BrokerID);
			strcpy(Posi.AccID,Trade.InvestorID);
			strcpy(Posi.ExchangeID,Trade.ExchangeID);
			if(Trade.Direction==THOST_FTDC_D_Buy){
				if(Trade.OffsetFlag==THOST_FTDC_OF_Open){
					Posi.BuyVolume+=Trade.Volume;
					Posi.TodayBuyVolume+=Trade.Volume;
					Posi.AvgBuyPrice = Trade.Price;
				}else{
					if(Trade.OffsetFlag==THOST_FTDC_OF_CloseToday || (Posi.SellVolume-Posi.TodaySellVolume)==0)
						Posi.TodaySellVolume-=Trade.Volume;
					Posi.SellVolume-=Trade.Volume;
					if (Posi.SellVolume == 0)
						Posi.AvgSellPrice = 0;
				}
				Posi.Volume+=Trade.Volume;
			}else{
				if(Trade.OffsetFlag==THOST_FTDC_OF_Open){
					Posi.SellVolume+=Trade.Volume;
					Posi.TodaySellVolume+=Trade.Volume;
					Posi.AvgSellPrice = Trade.Price;
				}else{
					if(Trade.OffsetFlag==THOST_FTDC_OF_CloseToday || (Posi.BuyVolume-Posi.TodayBuyVolume)==0)
						Posi.TodayBuyVolume-=Trade.Volume;
					Posi.BuyVolume-=Trade.Volume;
					if (Posi.BuyVolume == 0)
						Posi.AvgBuyPrice = 0;
				}
				Posi.Volume-=Trade.Volume;
			}
			if (Posi.BuyVolume > Posi.SellVolume)
				Posi.Price = Posi.AvgBuyPrice;
			else
				Posi.Price = Posi.AvgSellPrice;
			mPositionIndex[Posi.InstrumentID] = vPositions.size();
			vPositions.push_back(Posi);
		}
		switch(working_window){
		case WIN_FILLLIST:
			filllist_redraw();
			break;
		case WIN_POSITION:
			positionlist_redraw();
			break;
		case WIN_ORDER:
			order_redraw();
			break;
		default:
			break;
		}
	}
}

void CTradeRsp::HandleErrRtnOrderInsert(CThostFtdcInputOrderField& InputOrder, CThostFtdcRspInfoField& RspInfo)
{
	if(RspInfo.ErrorID!=0){
		status_print("报单拒绝:%s",RspInfo.ErrorMsg);
		std::vector<CThostFtdcOrderField>::iterator iter;
		for(iter=vOrders.begin();iter!=vOrders.end();iter++){
			if(iter->FrontID==TradeFrontID && iter->SessionID==TradeSessionID && strcmp(iter->OrderRef,InputOrder.OrderRef)==0){
				if(iter->OrderStatus==THOST_FTDC_OST_Canceled)	// 如果已经撤消,则不再重复处理
					break;
				std::vector<stPosition_t>::iterator iterPosi;
				for(iterPosi=vPositions.begin();iterPosi!=vPositions.end();iterPosi++){
					if(strcmp(InputOrder.InvestorID,iterPosi->AccID)==0 && strcmp(InputOrder.InstrumentID,iterPosi->InstrumentID)==0)
						break;
				}
				if(iterPosi!=vPositions.end()){  // 本Session中发出的定单肯定会有持仓记录
					//委托失败后释放冻结仓位
					if(InputOrder.Direction==THOST_FTDC_D_Buy){
						if(InputOrder.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(InputOrder.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->SellVolume-iterPosi->TodaySellVolume)==0)
								iterPosi->TodayFrozenSellVolume-=iter->VolumeTotal;
							iterPosi->FrozenSellVolume-=iter->VolumeTotal;
						}
						iterPosi->BuyingVolume-=iter->VolumeTotal;
					}else{
						if(InputOrder.CombOffsetFlag[0]!=THOST_FTDC_OF_Open){
							if(InputOrder.CombOffsetFlag[0]==THOST_FTDC_OF_CloseToday || (iterPosi->BuyVolume-iterPosi->TodayBuyVolume)==0)
								iterPosi->TodayFrozenBuyVolume-=iter->VolumeTotal;
							iterPosi->FrozenBuyVolume-=iter->VolumeTotal;
						}
						iterPosi->SellingVolume-=iter->VolumeTotal;
					}
				}
				iter->OrderStatus=THOST_FTDC_OST_Canceled;
				strcpy(iter->StatusMsg,RspInfo.ErrorMsg);
				break;
			}
		}
		switch(working_window){
		case WIN_ORDERLIST:
			orderlist_redraw();
			break;
		case WIN_ORDER:
			order_redraw();
			break;
		case WIN_POSITION:
			positionlist_redraw();
			break;
		default:
			break;
		}
	}
}
void CTradeRsp::HandleErrRtnOrderAction(CThostFtdcOrderActionField& OrderAction, CThostFtdcRspInfoField& RspInfo)
{
	if(strlen(OrderAction.InstrumentID)>0){
		status_print("撤单拒绝:%s",OrderAction.StatusMsg);
		std::vector<CThostFtdcInputOrderActionField>::iterator iter;
		for(iter=vCancelingOrders.begin();iter!=vCancelingOrders.end();iter++){
			if(iter->FrontID==OrderAction.FrontID && iter->SessionID==OrderAction.SessionID && strcmp(iter->OrderRef,OrderAction.OrderRef)==0){
				vCancelingOrders.erase(iter);	// 移除正在撤消的报单
				break;
			}
		}

// 		std::vector<CThostFtdcOrderField>::iterator iterOrder;
// 		for(iterOrder=vOrders.begin();iterOrder!=vOrders.end();iterOrder++){
// 			if(strcmp(iterOrder->InstrumentID,OrderAction.->InstrumentID)==0 && iterOrder->FrontID==OrderAction.->FrontID && iterOrder->SessionID==OrderAction.->SessionID && strcmp(iterOrder->OrderRef,OrderAction.->OrderRef)==0){
// 				iterOrder->OrderStatus=THOST_FTDC_OST_Canceled;	// 更新报单状态
// 				break;
// 			}
// 		}
		switch(working_window){
		case WIN_ORDERLIST:
			orderlist_redraw();
			break;
		case WIN_ORDER:
			order_redraw();
			break;
		case WIN_POSITION:
			positionlist_redraw();
			break;
		default:
			break;
		}
	}
}

// Market
void CMarketRsp::HandleFrontConnected()
{
	status_print("行情通道已连接.");

	MarketConnectionStatus=CONNECTION_STATUS_CONNECTED;
	CThostFtdcReqUserLoginField Req{};
	
	for(auto & vquote : vquotes)
		vquote.subscribed=false;

	memset(&Req,0x00,sizeof(Req));
	strcpy(Req.BrokerID,broker);
	strcpy(Req.UserID,user);
	strcpy(Req.Password,passwd);
	//sprintf(Req.UserProductInfo,"%s %s",APP_ID,APP_VERSION);
	m_pMarketReq->ReqUserLogin(&Req,0);
}
void CMarketRsp::HandleFrontDisconnected(int nReason)
{
	status_print("行情通道已断开.");
	MarketConnectionStatus=CONNECTION_STATUS_DISCONNECTED;
	switch(working_window){
	case WIN_MAINBOARD:
		refresh_screen();
		break;
	case WIN_ORDER:
		order_refresh_screen();
		break;
	case WIN_COLUMN_SETTINGS:
		column_settings_refresh_screen();
		break;
	case WIN_SYMBOL:
		symbol_refresh_screen();
		break;
	case WIN_ORDERLIST:
		orderlist_refresh_screen();
		break;
	case WIN_FILLLIST:
		filllist_refresh_screen();
		break;
	case WIN_POSITION:
		positionlist_refresh_screen();
		break;
	case WIN_MONEY:
		acclist_refresh_screen();
		break;
	default:
		break;
	}
}
void CMarketRsp::HandleRspUserLogin(CThostFtdcRspUserLoginField& RspUserLogin,CThostFtdcRspInfoField& RspInfo,int nRequestID,bool bIsLast)
{
	if(RspInfo.ErrorID!=0){
		status_print("行情通道登录失败:%s",RspInfo.ErrorMsg);
		MarketConnectionStatus=CONNECTION_STATUS_LOGINFAILED;
		display_status();
		return;
	}
	status_print("行情通道登录成功.");
	MarketConnectionStatus=CONNECTION_STATUS_LOGINOK;
	display_status();

	switch(working_window){
	case WIN_MAINBOARD:
		{
			for(size_t i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
				subscribe(i);
		}
		break;
	case WIN_ORDER:
		subscribe(order_symbol_index);
		break;
	case WIN_COLUMN_SETTINGS:
		break;
	default:
		break;
	}
}
void CMarketRsp::HandleRspUserLogout(CThostFtdcUserLogoutField& UserLogout,CThostFtdcRspInfoField& RspInfo,int nRequestID,bool bIsLast)
{
}

void CMarketRsp::HandleRtnDepthMarketData(CThostFtdcDepthMarketDataField& DepthMarketData)
{
	auto iter = mInstrumentIndex.find(DepthMarketData.InstrumentID);
	if (iter == mInstrumentIndex.end())
		return;
	size_t i = iter->second;
	
	if(vquotes[i].DepthMarketData.Volume!=DepthMarketData.Volume)
		vquotes[i].trade_volume=DepthMarketData.Volume-vquotes[i].DepthMarketData.Volume;
	if(strcmp(vquotes[i].exchange_id,"CZCE")!=0)
		vquotes[i].DepthMarketData.AveragePrice/=vquotes[i].Instrument.VolumeMultiple;
	memcpy(&vquotes[i].DepthMarketData, &DepthMarketData, sizeof(DepthMarketData));

	switch(working_window){
	case WIN_MAINBOARD:
		display_quotation(i);
		break;
	case WIN_ORDER:
		order_display_quotation(vquotes[i].product_id);
	default:
		break;
	}
}
int subscribe(size_t index)
{
	switch(working_window){
	case WIN_MAINBOARD:
		{
			if(vquotes[index].subscribed)
				break;
			char *ppInstrumentID[1]={(char *)vquotes[index].Instrument.InstrumentID};
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				break;
			vquotes[index].subscribed=true;
		}
		break;
	case WIN_ORDER:
		{
			if(order_symbol_index != index)
				break;
			
			if(vquotes[index].subscribed)
				break;
			char *ppInstrumentID[1]={(char *)vquotes[index].Instrument.InstrumentID };
			if(pMarketRsp->m_pMarketReq->SubscribeMarketData(ppInstrumentID, 1)<0)
				break;
			vquotes[index].subscribed=true;
		}
		break;
	default:
		break;
	}
	
	return 0;
}
int unsubscribe(size_t index)
{
	char *ppInstrumentID[1];

	if(index==UINT_MAX){
		for(auto & vquote : vquotes){
			if(vquote.subscribed){
				ppInstrumentID[0]=(char *)vquote.Instrument.InstrumentID;
				if(pMarketRsp->m_pMarketReq->UnSubscribeMarketData(ppInstrumentID, 1)<0)
					return -1;
				vquote.subscribed=false;
			}
		}	
		return 0;
	}
	ppInstrumentID[0] = (char*)vquotes[index].Instrument.InstrumentID;
	pMarketRsp->m_pMarketReq->UnSubscribeMarketData(ppInstrumentID, 1);
	vquotes[index].subscribed=false;
	
	return 0;
}

void corner_refresh_screen()
{	
	int y,x;

	if(working_window!=WIN_MAINBOARD)
		return;

	if(corner_win)
		delwin(corner_win);

	getmaxyx(stdscr,y,x);
	corner_win=newwin(corner_max_lines+3,corner_max_cols+2,y-(corner_max_lines+3)-1,x-(corner_max_cols+2));
	box(corner_win,'|','-');
	mvwaddch(corner_win,0,0,'+');
	mvwaddch(corner_win,0,corner_max_cols+1,'+');
	mvwaddch(corner_win,corner_max_lines+2,0,'+');
	mvwaddch(corner_win,corner_max_lines+2,corner_max_cols+1,'+');
// 	nodelay(stdscr,TRUE);
// 	keypad(stdscr,TRUE);
// 	noecho();
// 	curs_set(0);
// 	scrollok(stdscr,TRUE);
// 	clear();
	corner_redraw();
}

void corner_redraw()
{
	int y,x;

	werase(corner_win);
	getmaxyx(stdscr,y,x);
	corner_win=newwin(corner_max_lines+3,corner_max_cols+2,y-(corner_max_lines+3)-1,x-(corner_max_cols+2));
	box(corner_win,'|','-');
	mvwaddch(corner_win,0,0,'+');
	mvwaddch(corner_win,0,corner_max_cols+1,'+');
	mvwaddch(corner_win,corner_max_lines+2,0,'+');
	mvwaddch(corner_win,corner_max_lines+2,corner_max_cols+1,'+');
	corner_display_input();
	corner_display_matches();
	corner_display_focus();
}

void corner_display_input()
{
	int y,x;

	getmaxyx(stdscr,y,x);
	mvwprintw(corner_win,1,1,strsearching,"%s");
}

void corner_display_matches()
{
	size_t i,j;
	size_t y,x;
	
	getmaxyx(stdscr,y,x);

// 	if(strlen(strsearching)==0)
// 		return;
	for(i=corner_curr_pos,j=0;i<vquotes.size() && j<5;i++){
		if(_strnicmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0){
			if(j==0 && strlen(strsearching)>0){
				mvwprintw(corner_win,j+1,strlen(strsearching)+1,"%s",vquotes[i].product_id+strlen(strsearching));
				mvwchgat(corner_win,j+1,strlen(strsearching)+1,strlen(vquotes[i].product_id)-strlen(strsearching),A_REVERSE,0,NULL);
				strcpy(strmatch,vquotes[i].product_id);
			}
			mvwprintw(corner_win,j+2,1,"%s",vquotes[i].product_id);
			j++;
		}
	}
}

void corner_destroy()
{
	delwin(corner_win);
	corner_win=NULL;
}

void corner_choose_item()
{
	if(corner_curr_line>0){// selected
		size_t i,j;
		
		for(i=corner_curr_pos,j=0;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0){
				j++;
				if(j==corner_curr_line){	// found
					corner_destroy();
					refresh_screen();
					focus_quotation(i);
					break;
				}

			}
		}
	}else{// unselected
		
		for(size_t i=0;i<vquotes.size();i++){
			if(strcmp(vquotes[i].product_id,strmatch)==0){	// found
				corner_destroy();
				refresh_screen();
				focus_quotation(i);
				return;
			}
		}

		// not found
		corner_destroy();
		refresh_screen();
	}
}
int on_key_pressed_corner(int ch)
{
	int Num,Cmd;
	
	corner_input[strlen(corner_input)+1]=0;
	corner_input[strlen(corner_input)]=ch;
	if(input_parse_corner(&Num,&Cmd)<0)
		return 0;
	
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
		corner_destroy();
		refresh_screen();
		return 0;
	case KEYBOARD_ENTER:	// Enter
		corner_choose_item();
		return 0;
	case KEYBOARD_DELETE:
		corner_delete_char_before_current_pos();
		break;
	case KEYBOARD_REFRESH:		// ^L
		refresh_screen();
		corner_refresh_screen();
		break;
	case KEYBOARD_DOWN:		// ^N
	case KEYBOARD_NEXT:
		corner_move_forward_1_line();
		break;
	case KEYBOARD_PREVIOUS:		// ^P
	case KEYBOARD_UP:
		corner_move_backward_1_line();
		break;
	case KEYBOARD_LEFT:
	case KEYBOARD_CTRL_B:		// ^B
		corner_move_left_1_pos();
		break;
	case KEYBOARD_RIGHT:
	case KEYBOARD_CTRL_F:		// ^F
		corner_move_right_1_pos();
		break;
	case KEYBOARD_CTRL_D:		// ^D
		corner_delete_char_at_current_pos();
		break;
	case 0:
		corner_research();
		break;
	default:
		break;
	}
	return 0;
}

void corner_research()
{
	corner_curr_line=0,corner_curr_col=1,corner_max_lines=5,corner_max_cols=20;
	corner_curr_pos=0,corner_curr_col_pos=0;
	corner_redraw();
}
void corner_move_left_1_pos()
{
	if(corner_curr_col!=1){
		corner_curr_col--;
	}
	corner_redraw();
}

void corner_move_right_1_pos()
{
	if(corner_curr_col!=corner_max_cols){
		corner_curr_col++;
	}
	corner_redraw();
}

void corner_delete_char_at_current_pos()
{

}

void corner_delete_char_before_current_pos()
{
	if(strlen(strsearching)>0)
		strsearching[strlen(strsearching)-1]='\0';
	corner_research();
}

int input_parse_corner(int *num,int *cmd)
{
	char* p=corner_input;

	*num=0;
	if(isprint(*p)){
		if(strlen(strsearching)==corner_max_cols){
			*p='\0';
			return -1;
		}
		strsearching[strlen(strsearching)+1]='\0';
		strsearching[strlen(strsearching)]=*p;
		*cmd=0;
	}else{
		*cmd=*p;
	}
	*p='\0';
	
	return 0;
}

void corner_move_forward_1_line()
{
	if(corner_curr_line==0){	// first select
		size_t i;
		
		for(i=corner_curr_pos;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0)
				break;
		}
		if(i==vquotes.size())
			return;
		corner_curr_line=1;
		corner_curr_pos=i;
		corner_redraw();
		return;
	}

	size_t i,j;
	
	for(i=corner_curr_pos,j=0;i<vquotes.size() && j<=corner_curr_line;i++){
		if(strncmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0)
			j++;
	}
	if(j<=corner_curr_line)	// Already bottom
		return;

	if(corner_curr_line!=corner_max_lines){
		corner_curr_line++;
	}else{
		size_t next_pos;
		
		for(i=corner_curr_pos,j=0;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0){
				j++;
				if(j==2)
					next_pos=i;
				if(j>corner_curr_line)
					break;
			}
		}
		if(j<corner_curr_line)	// Already bottom
			return;
		corner_curr_pos=next_pos;
	}
	corner_redraw();
}

void corner_move_backward_1_line()
{
	if(corner_curr_line==0){	// first select
		size_t i;
		
		for(i=corner_curr_pos;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0)
				break;
		}
		if(i==vquotes.size())
			return;
		corner_curr_line=1;
		corner_curr_pos=i;
		corner_redraw();
		return;
	}
	
	if(corner_curr_line==1){
		int i;
		
		for(i=corner_curr_pos-1;i>=0;i--){
			if(strncmp(vquotes[i].product_id,strsearching,strlen(strsearching))==0)
				break;
		}
		if(i<0)	// Already top
			return;
		corner_curr_pos=i;
	}else{
		corner_curr_line--;
	}

	corner_redraw();
}

void corner_reset()
{
	corner_curr_line=0,corner_curr_col=1,corner_max_lines=5,corner_max_cols=20;
	corner_curr_pos=0,corner_curr_col_pos=0;
	memset(corner_input,0x00,sizeof(corner_input));
	memset(strsearching,0x00,sizeof(strsearching));
	memset(strmatch,0x00,sizeof(strmatch));
}

void corner_display_focus()
{
	if(corner_curr_line!=0)
		mvwchgat(corner_win,corner_curr_line+1,1,corner_max_cols,A_REVERSE,0,NULL);
}


// Order Corner

void order_corner_refresh_screen()
{	
	int y,x;

	if(working_window!=WIN_ORDER)
		return;

	if(order_corner_win)
		delwin(order_corner_win);

	getmaxyx(stdscr,y,x);
	order_corner_win=newwin(order_corner_max_lines+3,order_corner_max_cols+2,y-(order_corner_max_lines+3)-1,x-(order_corner_max_cols+2));
	box(order_corner_win,'|','-');
	mvwaddch(order_corner_win,0,0,'+');
	mvwaddch(order_corner_win,0,order_corner_max_cols+1,'+');
	mvwaddch(order_corner_win,order_corner_max_lines+2,0,'+');
	mvwaddch(order_corner_win,order_corner_max_lines+2,order_corner_max_cols+1,'+');
// 	nodelay(stdscr,TRUE);
// 	keypad(stdscr,TRUE);
// 	noecho();
// 	curs_set(0);
// 	scrollok(stdscr,TRUE);
// 	clear();
	order_corner_redraw();
}

void order_corner_redraw()
{
	int y,x;

	werase(order_corner_win);
	getmaxyx(stdscr,y,x);
	order_corner_win=newwin(order_corner_max_lines+3,order_corner_max_cols+2,y-(order_corner_max_lines+3)-1,x-(order_corner_max_cols+2));
	box(order_corner_win,'|','-');
	mvwaddch(order_corner_win,0,0,'+');
	mvwaddch(order_corner_win,0,order_corner_max_cols+1,'+');
	mvwaddch(order_corner_win,order_corner_max_lines+2,0,'+');
	mvwaddch(order_corner_win,order_corner_max_lines+2,order_corner_max_cols+1,'+');
	order_corner_display_input();
	order_corner_display_matches();
	order_corner_display_focus();
}

void order_corner_display_input()
{
	int y,x;

	getmaxyx(stdscr,y,x);
	mvwprintw(order_corner_win,1,1,order_strsearching,"%s");
}

void order_corner_display_matches()
{
	size_t i,j;
	size_t y,x;
	
	getmaxyx(stdscr,y,x);

// 	if(strlen(strsearching)==0)
// 		return;
	for(i=order_corner_curr_pos,j=0;i<vquotes.size() && j<5;i++){
		if(_strnicmp(vquotes[i].product_id,order_strsearching,strlen(order_strsearching))==0){
			if(j==0 && strlen(order_strsearching)>0){
				mvwprintw(order_corner_win,j+1,strlen(order_strsearching)+1,"%s",vquotes[i].product_id+strlen(order_strsearching));
				mvwchgat(order_corner_win,j+1,strlen(order_strsearching)+1,strlen(vquotes[i].product_id)-strlen(order_strsearching),A_REVERSE,0,NULL);
				strcpy(order_strmatch,vquotes[i].product_id);
			}
			mvwprintw(order_corner_win,j+2,1,"%s",vquotes[i].product_id);
			j++;
		}
	}
}

void order_corner_destroy()
{
	delwin(order_corner_win);
	order_corner_win=NULL;
}

void order_corner_choose_item()
{
	if(order_corner_curr_line>0){// selected
		size_t i,j;
		
		for(i=order_corner_curr_pos,j=0;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,order_strsearching,strlen(order_strsearching))==0){
				j++;
				if(j==order_corner_curr_line){	// found
					strcpy(order_last_symbol,vquotes[order_symbol_index].product_id);
					order_corner_destroy();
					order_symbol_index=i;
					order_curr_price=0;
					order_page_top_price=0;
					order_refresh_screen();
					order_centralize_current_price();
					subscribe(order_symbol_index);
					break;
				}
			}
		}
	}else{// unselected
		for(size_t i=0;i<vquotes.size();i++){
			if(strcmp(vquotes[i].product_id,order_strmatch)==0){	// found
				strcpy(order_last_symbol,vquotes[order_symbol_index].product_id);
				order_corner_destroy();
				order_symbol_index=i;
				order_curr_price=0;
				order_page_top_price=0;
				order_refresh_screen();
				order_centralize_current_price();
				subscribe(order_symbol_index);
				return;
			}
		}

		// not found
		order_corner_destroy();
		order_refresh_screen();
	}
}
int order_on_key_pressed_corner(int ch)
{
	int Num,Cmd;
	
	order_corner_input[strlen(order_corner_input)+1]=0;
	order_corner_input[strlen(order_corner_input)]=ch;
	if(order_input_parse_corner(&Num,&Cmd)<0)
		return 0;
	
	if(Num==0)
		Num=1;
	switch(Cmd){
	case KEYBOARD_ESC:	// ESC
		order_corner_destroy();
		order_refresh_screen();
		return 0;
	case KEYBOARD_ENTER:	// Enter
		order_corner_choose_item();
		return 0;
	case KEYBOARD_DELETE:
		order_corner_delete_char_before_current_pos();
		break;
	case KEYBOARD_REFRESH:		// ^L
		order_refresh_screen();
		order_corner_refresh_screen();
		break;
	case KEYBOARD_DOWN:		// ^N
	case KEYBOARD_NEXT:
		order_corner_move_forward_1_line();
		break;
	case KEYBOARD_PREVIOUS:		// ^P
	case KEYBOARD_UP:
		order_corner_move_backward_1_line();
		break;
	case KEYBOARD_LEFT:
	case KEYBOARD_CTRL_B:		// ^B
		order_corner_move_left_1_pos();
		break;
	case KEYBOARD_RIGHT:
	case KEYBOARD_CTRL_F:		// ^F
		order_corner_move_right_1_pos();
		break;
	case KEYBOARD_CTRL_D:		// ^D
		corner_delete_char_at_current_pos();
		break;
	case 0:
		order_corner_research();
		break;
	default:
		break;
	}
	return 0;
}

void order_corner_research()
{
	order_corner_curr_line=0,order_corner_curr_col=1,order_corner_max_lines=5,order_corner_max_cols=20;
	order_corner_curr_pos=0,order_corner_curr_col_pos=0;
	order_corner_redraw();
}
void order_corner_move_left_1_pos()
{
	if(order_corner_curr_col!=1){
		order_corner_curr_col--;
	}
	order_corner_redraw();
}

void order_corner_move_right_1_pos()
{
	if(order_corner_curr_col!=order_corner_max_cols){
		order_corner_curr_col++;
	}
	order_corner_redraw();
}

void order_corner_delete_char_at_current_pos()
{

}

void order_corner_delete_char_before_current_pos()
{
	if(strlen(order_strsearching)>0)
		order_strsearching[strlen(order_strsearching)-1]='\0';
	order_corner_research();
}

int order_input_parse_corner(int *num,int *cmd)
{
	char* p=order_corner_input;

	*num=0;
	if(isprint(*p)){
		if(strlen(order_strsearching)==order_corner_max_cols){
			*p='\0';
			return -1;
		}
		order_strsearching[strlen(order_strsearching)+1]='\0';
		order_strsearching[strlen(order_strsearching)]=*p;
		*cmd=0;
	}else{
		*cmd=*p;
	}
	*p='\0';
	
	return 0;
}

void order_corner_move_forward_1_line()
{
	if(order_corner_curr_line==0){	// first select
		size_t i;
		
		for(i=order_corner_curr_pos;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,order_strsearching,strlen(order_strsearching))==0)
				break;
		}
		if(i==vquotes.size())
			return;
		order_corner_curr_line=1;
		order_corner_curr_pos=i;
		order_corner_redraw();
		return;
	}

	size_t i,j;
	
	
	for(i=order_corner_curr_pos,j=0;i<vquotes.size() && j<=order_corner_curr_line;i++){
		if(strncmp(vquotes[i].product_id,order_strsearching,strlen(order_strsearching))==0)
			j++;
	}
	
	if(j<=order_corner_curr_line)	// Already bottom
		return;

	if(order_corner_curr_line!=order_corner_max_lines){
		order_corner_curr_line++;
	}else{
		size_t next_pos;
		
		for(i=order_corner_curr_pos,j=0;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,order_strsearching,strlen(order_strsearching))==0){
				j++;
				if(j==2)
					next_pos=i;
				if(j>order_corner_curr_line)
					break;
			}
		}
		
		if(j<order_corner_curr_line)	// Already bottom
			return;
		order_corner_curr_pos=next_pos;
	}
	order_corner_redraw();
}

void order_corner_move_backward_1_line()
{
	if(order_corner_curr_line==0){	// first select
		size_t i;
		
		for(i=order_corner_curr_pos;i<vquotes.size();i++){
			if(strncmp(vquotes[i].product_id,order_strsearching,strlen(order_strsearching))==0)
				break;
		}
		if(i==vquotes.size())
			return;
		
		order_corner_curr_line=1;
		order_corner_curr_pos=i;
		order_corner_redraw();
		return;
	}
	
	if(order_corner_curr_line==1){
		int i;
		
		for (i = order_corner_curr_pos - 1; i >= 0; i--) {
			if (strncmp(vquotes[i].product_id, order_strsearching, strlen(order_strsearching)) == 0)
				break;
		}
		if(i<0)	// Already top
			return;
		order_corner_curr_pos=i;
	}else{
		order_corner_curr_line--;
	}

	order_corner_redraw();
}

void order_corner_reset()
{
	order_corner_curr_line=0,order_corner_curr_col=1,order_corner_max_lines=5,order_corner_max_cols=20;
	order_corner_curr_pos=0,order_corner_curr_col_pos=0;
	memset(order_corner_input,0x00,sizeof(order_corner_input));
	memset(order_strsearching, 0x00, sizeof(order_strsearching));
	memset(order_strmatch, 0x00, sizeof(order_strmatch));
}

void order_corner_display_focus()
{
	if(order_corner_curr_line!=0)
		mvwchgat(order_corner_win,order_corner_curr_line+1,1,order_corner_max_cols,A_REVERSE,0,NULL);
}
