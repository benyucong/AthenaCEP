#include "../Query.h"
#include "../PatternMatcher.h"
#include "../EventStream.h"
#include "../_shared/PredicateMiner.h"
#include "../_shared/MonitorThread.h"
#include "../_shared/GlobalClock.h"
//#include "Python.h" 

#include "../freegetopt/getopt.h"
#include "../NormalDistGenChangePattern.h"

#include <vector>
#include <chrono>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <assert.h>
#include <iostream>
#include <random>
#include <set>
#include <queue>

using namespace std;
using namespace std::chrono;

//static time_point<high_resolution_clock> g_BeginClock;
time_point<high_resolution_clock> g_BeginClock;
uint64_t NumFullMatch = 0;
uint64_t NumHighLatency = 0;
uint64_t NumPartialMatch= 0;
uint64_t NumShedPartialMatch= 0;
uint64_t A[11];
uint64_t Ac[11];
uint64_t B[11];
uint64_t Bc[11];
uint64_t C[21];

long int RealTimeLatency = 0;
int G_numTimeslice=1;
int G_numCluster = 4;
default_random_engine m_generator;
uniform_int_distribution<int> m_distribution(1,100); 
uniform_int_distribution<int> random_input_shedding_distribution(0,1); 

bool eventB = false;
bool eventZ= false;
uint64_t lastEventBVersion = 0;
uint64_t lastEventBTS = 0;
uint64_t PMSheddingTimer = 0;
int PMSheddingDice = 1;

uint64_t ACCLantency = 0;

inline void init_utime() 
{
	g_BeginClock = high_resolution_clock::now();
}

inline uint64_t current_utime() 
{ 
	return duration_cast<microseconds>(high_resolution_clock::now() - g_BeginClock).count();
}

class CepMatch
{
public:
	//CepMatch(RingBuffer<NormalEvent>& Q) : m_Query(0), m_DefAttrId(0), m_DefAttrOffset(0), m_NextMinerUpdateTime(0), RawEventQueue(Q)
	CepMatch(queue<NormalEvent>& Q) : m_Query(0), m_DefAttrId(0), m_DefAttrOffset(0), m_NextMinerUpdateTime(0), RawEventQueue(Q)
	{
        lastSheddingTime = 0;
        loadCnt = 0;
        time = 60000000;
        acctime = time;
        Rtime = time/50000;
        latency = 0;
        cntFullMatch = 1;
        m_RandomInputSheddingFlag = false;
        m_InputSeddingSwitcher = false;
        m_PMSheddingSwitcher = false;
	}

	~CepMatch()
	{

	}


	bool init(const char* _defFile, const char* _queryName, const char* _miningPrefix, bool _generateTimeoutEvents, bool _appendTimestamp)
	{
		StreamEvent::setupStdIo();

		if (!m_Definition.loadFile(_defFile))
		{
			fprintf(stderr, "failed to load definition file %s\n", _defFile);
			return false;
		}

		m_Query = !_queryName ? m_Definition.query((size_t)0) : m_Definition.query(_queryName);
		if (!m_Query)
		{
			fprintf(stderr, "query not found");
			return false;
		}

		if(_miningPrefix)
		{
			m_MiningPrefix = _miningPrefix;
			m_Miner.reset(new PredicateMiner(m_Definition, *m_Query));
			_generateTimeoutEvents = true; // important for mini

			for (size_t i = 0; i < m_Query->events.size() - 1; ++i)
			{
				uint32_t eventType = m_Definition.findEventDecl(m_Query->events[i].type.c_str());
				m_Miner->initList(i * 2 + 0, eventType);
				m_Miner->initList(i * 2 + 1, eventType);
			}

			unsigned numCores = thread::hardware_concurrency();
			m_Miner->initWorkerThreads(std::min(numCores - 1, 16u));

			const uint64_t one_min = 60 * 1000 * 1000;
			m_NextMinerUpdateTime = current_utime() + one_min / 6;
		}

		QueryLoader::Callbacks cb;
		cb.insertEvent[PatternMatcher::ST_ACCEPT] = bind(&CepMatch::write_event, this, false, placeholders::_1, placeholders::_2);

		if(_generateTimeoutEvents)
			cb.timeoutEvent = bind(&CepMatch::write_event, this, true, placeholders::_1, placeholders::_2);

		if (!m_Definition.setupPatternMatcher(m_Query, m_Matcher, cb))
		{
			return false;
		}

		m_Query->generateCopyList(m_Query->returnAttr, m_OutEventAttrSrc);

		m_ResultEventType = m_Definition.findEventDecl(m_Query->returnName.c_str());
		m_ResultEventTypeHash = StreamEvent::hash(m_Query->returnName);
		m_ResultAttributeCount = (uint8_t)m_OutEventAttrSrc.size();
		m_GenerateTimeoutEvents = _generateTimeoutEvents;
		m_AppendTimestamp = _appendTimestamp;

		return true;
	}

	//bool processEvent(NormalDistGen& StreamGen)
	bool processEvent(uint64_t _eventCnt = 0)
	{
        //cout << "[processEvent] flag1" << endl;
		StreamEvent event;
        NormalEvent RawEvent;
        //NormalEvent RawEvent1;
        //cout << "[processEvent] flag2" << endl;
        if(!RawEventQueue.empty())
        {
            RawEvent = RawEventQueue.front();
            RawEventQueue.pop();

            //RawEvent1 = RawEventQueue.front();
            //RawEventQueue.pop_front();
        }
        else{ 
            cout << "event stream queue is empty now!" << endl;
            return false;
        }

        //cout << "[processEvent] flag3" << endl;
		//if (!event.read())
	    //		return false;
        //cout << "StreamGen.isStop " <<  StreamGen.isStop() << endl;
        //while(RawEventQueue.empty() && !StreamGen.isStop())
        //while(RawEventQueue.empty())
        //    this_thread::sleep_for(chrono::milliseconds(5));
        //if(RawEventQueue.empty() && StreamGen.isStop())
        //    return false;
        //
        

       // bool eventB = false;
       // if(RawEvent.name == "B" && RawEvent1.name == "Z")
       // {
       //     RawEventQueue.pop_front();
       //     eventZ=true;
       //     if(!eventB) 
       //     {
       //         cout << "detect of event B: " << RawEvent.ArrivalQTime << endl;
       //         eventB = true;
       //     }
       // }
        
        // fine control for monitoring event B and PM/Input shedding
        
        

        event.attributes[0] = RawEvent.ArrivalQTime;
        event.attributes[1] = RawEvent.v1;
        event.attributes[2] = RawEvent.ID;
        //event.attributes[2] = RawEvent.v2;
        //event.attributes[3] = RawEvent.ID;
        //event.attributes[3] = 1;

        event.typeIndex = m_Definition.findEventDecl(RawEvent.name.c_str());

		event.attributes[Query::DA_ZERO] = 0;
		event.attributes[Query::DA_MAX] = numeric_limits<attr_t>::max();
		event.attributes[Query::DA_CURRENT_TIME] = current_utime();
		//event.attributes[Query::DA_CURRENT_TIME] = (uint64_t)duration_cast<microseconds>(high_resolution_clock::now() - g_BeginClock).count(); 
		event.attributes[Query::DA_OFFSET] = m_DefAttrOffset;
		event.attributes[Query::DA_ID] = m_DefAttrId;

        //random input shedding
        //if(m_RandomInputSheddingFlag)
        //{
        //    int dice_roll = m_distribution(m_generator);
        //    if(dice_roll == 1)
        //    {
        //       // cout << "shed input event" << endl;
        //        ++loadCnt;
        //        return true;
        //    }
        //}


        //cout << "[processEvent] flag4" << endl;
		const EventDecl* decl = m_Definition.eventDecl(event.typeIndex);
        //cout << "[processEvent] flag5" << endl;

		//assert(event.typeHash == StreamEvent::hash(decl->name));

        
        //cout << "matchEvent in " << event.typeIndex << endl;
        //cout << "[processEvent] flag6" << endl;

		m_Matcher.event(event.typeIndex, (attr_t*)event.attributes);

        if(_eventCnt == 230000)
        {
            m_printFMpoint = _eventCnt + 100;
            m_FM = NumFullMatch;
        }

        if(RawEvent.name == "S")
        {
            m_FM = NumFullMatch;
            m_printFMpoint = _eventCnt + 100; 
        }


        if(_eventCnt > 230000 && _eventCnt ==  m_printFMpoint)
        {
            cout <<  RawEvent.ArrivalQTime  << "," << NumFullMatch - m_FM << endl;
            m_printFMpoint += 100;
            m_FM = NumFullMatch;
        }
    
        //cout << "matchEvent out " << endl;
        //

        // ========================================================================
        // setting manully for State-based shedding for eventB
        // and for random input-based shedding
        if(RawEvent.name == "B")
        {
            if(lastEventBTS == 0)
                lastEventBTS = RawEvent.ArrivalQTime;
            if(lastEventBVersion == 0)
                lastEventBVersion = RawEvent.ArrivalQTime;
            if(PMSheddingTimer == 0)
                PMSheddingTimer = RawEvent.ArrivalQTime + uint64_t(0.5*G_EventBOn*1000000);
        }

//        if(PMSheddingTimer != 0 && RawEvent.ArrivalQTime >= PMSheddingTimer-5000)
//        {
//            //cout << "in cep_match processEvent perform shedding :" << RawEvent.ArrivalQTime <<  endl;
//            if(m_PMSheddingSwitcher)
//            {
//                if(PMSheddingDice%2 == 1)
//                    // shed partial match AB*
//                    loadCnt += m_Matcher.PMloadShedding(2);
//                else
//                    // shed paretial match A**
//                    loadCnt += m_Matcher.PMloadShedding(1);
//                ++PMSheddingDice;
//            }
//
//            if(m_InputSeddingSwitcher)
//            {
//                // perform input shedding
//                int ShedEventCnt = G_InputShedCnt;
//                while(ShedEventCnt)
//                {
//                   // while(RawEventQueue.empty())
//                    //{
//                         //this_thread::sleep_for(chrono::milliseconds(5));
//                     //    cout << "waiting for producer" << endl;
//                    //}
//                    if (RawEventQueue.empty())
//                        break;
//                    RawEventQueue.pop();
//                    ShedEventCnt--;
//                    loadCnt++;
//                }
//
//            }   
//            
//            PMSheddingTimer += uint64_t(0.5*(G_EventBOn+G_EventBOff)*1000000);
//        }

        //=========================================================================

		if (m_Miner)
		{
			m_Miner->flushMatch();
			m_Miner->addEvent(event.typeIndex, (const attr_t*)event.attributes);
			m_Miner->removeTimeouts(event.attributes[0]);
		}

		m_DefAttrId++;
		m_DefAttrOffset += event.offset;
		//assert(event.offset > 0);

		if (m_Miner && m_NextMinerUpdateTime <= event.attributes[Query::DA_CURRENT_TIME])
		{
			const uint64_t one_min = 60 * 1000 * 1000;
			m_NextMinerUpdateTime = event.attributes[Query::DA_CURRENT_TIME] + 10 * one_min;
			update_miner();
		}

       //cout << "processEvent out " << endl;

		return true;
	}

//    void printContribution()
//    {
//        for(auto&& it: m_Matcher.m_States)
//        {
//            std::cout << endl << "state contributions size == " << it.contributions.size() << endl;
//            for(auto&& iter: it.contributions)
//                cout << iter.first << "appears " << iter.second << "times" << endl;
//
//            std::cout << endl << "state consumptions size == " << it.consumptions.size() << endl;
//            for(auto&& iter: it.consumptions)
//                cout << iter.first << "appears " << iter.second << "times" << endl;
//            std::cout << endl << "state scoreTable size == " << it.scoreTable.size() << endl;
//
//            if(!it.attr.empty())
//            std::cout << "state size == " <<  it.attr.front().size() << endl;
//
//            
//        }
//        cout << "queue size " << RawEventQueue.size() << endl;
//    }
//
//    void printStatesSize()
//    {
//        for(auto && it: m_Matcher.m_States)
//            if(!it.attr.empty())
//                cout << "state " << it.ID << " size: " << it.attr.front().size() << endl;
//    }

    void PMSheddingOn() { m_PMSheddingSwitcher = true;}
    void PMSheddingOff() {m_PMSheddingSwitcher = false;}
    bool PMShedding() { return m_PMSheddingSwitcher;}

    void InputSheddingOn() {m_InputSeddingSwitcher = true;}
    void InputSheddingOff() {m_InputSeddingSwitcher= false;}
    bool InputShedding()  { return m_InputSeddingSwitcher;}
    
    uint64_t  RandomInputShedding(int _quota, uint64_t & _eventCnt) 
    {
        int ToShedCnt = _quota;
        
        while(ToShedCnt > 0 && !RawEventQueue.empty())
        {
            int dice_roll = m_distribution(m_generator);
            if(dice_roll <= 50)
            {
                RawEventQueue.pop();
                --ToShedCnt;
            }
            else
            {
               if(processEvent()) 
                   _eventCnt++;
            }
        }

        return _quota - ToShedCnt;
    }

    uint64_t  RandomInputShedding(double ratio, volatile uint64_t & _eventCnt) 
    {
        uint64_t ShedCnt = 0;
        
        while(!RawEventQueue.empty())
        {
            ++_eventCnt;
            if(RawEventQueue.front().name == "D") 
            {
                processEvent();
               // _eventCnt++;
                continue; 
            }
            int dice_roll = m_distribution(m_generator);
            if(dice_roll <=  (ratio *100) )
            {
                RawEventQueue.pop();
                ++ShedCnt;
                //_eventCnt++;
            }
            else
            {
               processEvent(); 
                 //_eventCnt++;
            }
        }

        return ShedCnt;
    }


   uint64_t  VLDB_03_InputShedding(int _quota, uint64_t & _eventCnt) 
   {
//       cout << "[VLDB_03_InputShedding] in " << endl;
       int ToShedCnt = _quota;
       NormalEvent RawEvent;
       while(ToShedCnt > 0 && !RawEventQueue.empty())
       {
           RawEvent = RawEventQueue.front(); 

           if(RawEvent.name == "A") 
           {
               if(RawEvent.v1 <= 33 || RawEvent.v1 > 53)
               {
                   RawEventQueue.pop();
                   --ToShedCnt;
               }
               else
               {
                   processEvent();
                   _eventCnt++; 
               }

           }
           else if(RawEvent.name == "B")
           {
               if(RawEvent.v2 > 54 || RawEvent.v2 < 43)
               {
                   RawEventQueue.pop();
                   --ToShedCnt;
               }
               else
               {
                   processEvent();
                   _eventCnt++;
               }

           }
           else
           {
               processEvent();
               _eventCnt++;
           }
       }

 //      cout << "[VLDB_03_InputShedding] succ leave shedding cnt : " << _quota -ToShedCnt << endl;

       return _quota - ToShedCnt;
   }


   uint64_t  VLDB_03_InputShedding(int ALowB, int AUpB, int BLowB, int BUpB, int CLowB, int CUpB, volatile uint64_t & _eventCnt) 
   {
//       cout << "[VLDB_03_InputShedding] in " << endl;
       uint64_t ShedCnt = 0;
       NormalEvent RawEvent;
       while(!RawEventQueue.empty())
       {
           RawEvent = RawEventQueue.front(); 
           

           if(RawEvent.name == "A") 
           {
               if(RawEvent.v1 < ALowB || RawEvent.v1 > AUpB)
               {
                   RawEventQueue.pop();
                   ++ShedCnt;
                   _eventCnt++;
               }
               else
               {
                   processEvent();
                   _eventCnt++; 
               }

           }
           else if(RawEvent.name == "B")
           {
               if(RawEvent.v1 < BLowB || RawEvent.v1 > BUpB)
               {
                   RawEventQueue.pop();
                   ++ShedCnt;
                   _eventCnt++;
               }
               else
               {
                   processEvent();
                   _eventCnt++;
               }

           }
           else if(RawEvent.name == "C")
           {

               if( RawEvent.v1 < CLowB || RawEvent.v1 > CUpB )
               {
                   RawEventQueue.pop();
                   ++ShedCnt;
                   _eventCnt++;
               }
               else
               {
                   processEvent();
                   _eventCnt++;
               }
           }
           else
           {
               processEvent();
               _eventCnt++;
           }
       }

 //      cout << "[VLDB_03_InputShedding] succ leave shedding cnt : " << _quota -ToShedCnt << endl;

       return ShedCnt;
   }

   uint64_t  VLDB_03_InputShedding(double _ratio, volatile uint64_t & _eventCnt) 
   {
//       cout << "[VLDB_03_InputShedding] in " << endl;
       uint64_t ShedCnt = 0;
       int ratio = int(_ratio * 100);
       NormalEvent RawEvent;

       switch (ratio)
       {
           case 10:
               {
                   const int   AUpB = 9;
                   const int   BUpB = 9;
                   const int   DiceUpB = int( double(10/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && RawEvent.v1 == 2 && dice_roll <= DiceUpB)
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;

           case 20:
               {
                   const int    AUpB = 8;
                   const int    BUpB = 8;
                   const int    DiceUpB = int( double(9/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && (RawEvent.v1 == 2 || (RawEvent.v1 ==3 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 30:
               {
                   const int     AUpB = 7;
                   const int     BUpB = 7;
                   const int     DiceUpB = int( double(8/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && ( (RawEvent.v1 >=2 && RawEvent.v1 <=3) || (RawEvent.v1 ==4 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 40:
               {
                   const int     AUpB = 6;
                   const int     BUpB = 6;
                   const int     DiceUpB = int( double(7/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && ( (RawEvent.v1 >=2 && RawEvent.v1 <=4) ||  (RawEvent.v1 ==10 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 50:
               {
                   const int     AUpB = 5;
                   const int     BUpB = 5;
                   const int     DiceUpB = int( double(6/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && ( (RawEvent.v1 >=2 && RawEvent.v1 <=4) || (RawEvent.v1 >= 9) ||  (RawEvent.v1 ==8 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 60:
               {
                   const int     AUpB = 4;
                   const int     BUpB = 4;
                   const int     DiceUpB = int( double(5/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && 
                               ( (RawEvent.v1 >=7 ) || (RawEvent.v1 ==2) ||  (RawEvent.v1 ==3 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 70:
               {
                   const int     AUpB = 3;
                   const int     BUpB = 3;
                   const int     DiceUpB = int( double(4/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && 
                               ( (RawEvent.v1 >=6 ) || (RawEvent.v1 ==2) ||  (RawEvent.v1 ==3 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 80:
               {
                   const int     AUpB = 2;
                   const int     BUpB = 2;
                   const int     DiceUpB = int( double(3/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && 
                               ( (RawEvent.v1 >=4 )  ||  (RawEvent.v1 ==2 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 90:
               {
                   const int     AUpB = 1;
                   const int     BUpB = 1;
                   const int     DiceUpB = int( double(2/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       int dice_roll =  m_distribution(m_generator);
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && 
                               ( (RawEvent.v1 >=3 )  ||  (RawEvent.v1 ==2 && dice_roll <= DiceUpB)))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           default:
               return 0;
               break;
       }

      
   }

   uint64_t  SmPMS_min_combo_additional_InputShedding(double _ratio, volatile uint64_t & _eventCnt) 
   {
       uint64_t ShedCnt = 0;
       int ratio = int(_ratio * 100);
       NormalEvent RawEvent;

       switch (ratio)
       {
           case 10:
           case 20:
           case 30:
           case 40:
           case 50:
               {
                   const int   AUpB = 9;
                   const int   BUpB = 9;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;

           case 60:
               {
                   const int     AUpB = 9;
                   const int     BUpB = 9;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" &&  (RawEvent.v1 == 3 || RawEvent.v1 ==4 ))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 70:
               {
                   const int     AUpB = 9;
                   const int     BUpB = 9;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" &&  RawEvent.v1 <=6 ) 
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 80:
               {
                   const int     AUpB = 9;
                   const int     BUpB = 9;
                   const int     DiceUpB = int( double(3/11) * 100);

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && ( RawEvent.v1 <=6   ||  RawEvent.v1 ==10))
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 90:
               {

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 !=1 && RawEvent.v1 != 9) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 !=1 && RawEvent.v1 != 9)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && RawEvent.v1 !=10)
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           default:
               return 0;
               break;
       }
   }

   uint64_t SmPMS_additional_InputShedding(double _ratio, volatile uint64_t & _eventCnt)
   {
       uint64_t ShedCnt = 0;
       int upAB = *m_PM_Booking.rbegin();
       //if(upAB > 10)
       //    upAB =10;
       //
       uint64_t printFMpoint  = 230000 + 100;
       uint64_t FM = NumFullMatch;
       
       NormalEvent RawEvent;
       while(!RawEventQueue.empty())
       {
           RawEvent = RawEventQueue.front();
           if(false && RawEvent.name == "A" && RawEvent.v1 >= upAB)
           {
               RawEventQueue.pop();
               ++ShedCnt;
               _eventCnt++;
           }
           else if(false && RawEvent.name == "B" && RawEvent.v1 >= upAB)
           {
               RawEventQueue.pop();
               ++ShedCnt;
               _eventCnt++;
           }
           else if(false && RawEvent.name == "C" && m_type_C_Booking[RawEvent.v1] == false) 
           {
               RawEventQueue.pop();
               ++ShedCnt;
               _eventCnt++;
           }
           else if(RawEvent.name == "S")
           {
               cout << "!!!!!! STREAM CHANGES!!!!!! " << endl;
               m_Matcher.clustering_classification_PM_shedding_semantic_setPMSheddingCombo(3);
               cout << m_Matcher.m_States[2].PMSheddingCombo << endl;
               ShedCnt += SmPMS_additional_InputShedding2(_ratio, _eventCnt);
           }
           else
           {
               processEvent(); 
               _eventCnt++;
           }

           if(_eventCnt > printFMpoint)
           {
               cout <<  RawEvent.ArrivalQTime  << "," << NumFullMatch - FM << endl; 
               printFMpoint += 100;
               FM = NumFullMatch;
           }
       }

       return ShedCnt;

   }

   uint64_t SmPMS_additional_InputShedding2(double _ratio, volatile uint64_t & _eventCnt)
   {
       uint64_t ShedCnt = 0;
       int lowAB = *m_PM_Booking2.begin();
       //if(upAB > 10)
       //    upAB =10;
       uint64_t printFMpoint  = _eventCnt + 100;
       uint64_t FM = NumFullMatch;
       NormalEvent RawEvent;
       while(!RawEventQueue.empty())
       {
           RawEvent = RawEventQueue.front();
           if(false && RawEvent.name == "A" && RawEvent.v1 < lowAB)
           {
               RawEventQueue.pop();
               ++ShedCnt;
               _eventCnt++;
           }
           else if(false && RawEvent.name == "B" && RawEvent.v1 < lowAB)
           {
               RawEventQueue.pop();
               ++ShedCnt;
               _eventCnt++;
           }
           else if(false && RawEvent.name == "C" && m_type_C_Booking2[RawEvent.v1] == false) 
           {
               RawEventQueue.pop();
               ++ShedCnt;
               _eventCnt++;
           }
           else
           {
               processEvent(); 
               _eventCnt++;
           }

           if(_eventCnt > printFMpoint)
           {
               cout <<  RawEvent.ArrivalQTime  << "," << NumFullMatch - FM << endl; 
               printFMpoint += 100;
               FM = NumFullMatch;
           }
       }

       return ShedCnt;

   }


   uint64_t  SmPMS_max_combo_additional_InputShedding(double _ratio, volatile uint64_t & _eventCnt) 
   {
       uint64_t ShedCnt = 0;
       int ratio = int(_ratio * 100);
       NormalEvent RawEvent;

       switch (ratio)
       {
           case 10:
           case 20:
           case 30:
           case 40:
           case 50:
               {
                   const int   AUpB = 9;
                   const int   BUpB = 9;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;

           case 60:
               {
                   const int     AUpB = 9;
                   const int     BUpB = 9;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" &&  RawEvent.v1 == 6)
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 70:
               {
                   const int     AUpB = 8;
                   const int     BUpB = 8;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C") 
                       {
                           
                           switch(RawEvent.v1)
                           {
                               case 2:
                               case 6:
                               case 10:
                                   RawEventQueue.pop();
                                   ++ShedCnt;
                                   _eventCnt++;
                                   break;
                               default:
                                   processEvent();
                                   _eventCnt++;
                                   break;
                           }
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 80:
               {
                   const int     AUpB = 7;
                   const int     BUpB = 7;

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > AUpB) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > BUpB)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C")
                       {
                           switch(RawEvent.v1)
                           {
                               case 2:
                               case 3:
                               case 6:
                               case 9:
                               case 10:
                                   RawEventQueue.pop();
                                   ++ShedCnt;
                                   _eventCnt++;
                                   break;
                               default:
                                   processEvent();
                                   _eventCnt++;
                                   break;

                           }
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           case 90:
               {

                   while(!RawEventQueue.empty())
                   {
                       RawEvent = RawEventQueue.front(); 


                       if(RawEvent.name == "A" && RawEvent.v1 > 4) 
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "B" && RawEvent.v1 > 4)
                       {
                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else if(RawEvent.name == "C" && RawEvent.v1 >=6)
                       {

                               RawEventQueue.pop();
                               ++ShedCnt;
                               _eventCnt++;
                       }
                       else
                       {
                           processEvent();
                           _eventCnt++;
                       }
                   }
                   return ShedCnt;
               }
               break;
           default:
               return 0;
               break;
       }
   }
   
   uint64_t  selectivity_InputShedding(double ratio, volatile uint64_t & _eventCnt) 
   {
       uint64_t ShedCnt = 0;



       const int dice_UP = ratio*100;

       NormalEvent RawEvent;
       while(!RawEventQueue.empty())
       {
           RawEvent = RawEventQueue.front(); 

            int dice_roll2 =  m_distribution(m_generator);

           //if(dice_roll < dice_UP )
           //{
               if(RawEvent.name == "A" && dice_roll2 <= dice_UP+2) 
               {
                   RawEventQueue.pop();
                   ++ShedCnt;
                   _eventCnt++;
               }
               else if(RawEvent.name == "B" && dice_roll2 <= dice_UP+2)
               {
                   RawEventQueue.pop();
                   ++ShedCnt;
                   _eventCnt++;
               }
               else if(RawEvent.name == "C" && dice_roll2 <= dice_UP-2)
               {
                   RawEventQueue.pop();
                   ++ShedCnt;
                   _eventCnt++;
               }
               else
               {
                   processEvent();
                   _eventCnt++;
               }
           //}
           //else
           //{
           //    processEvent();
           //    _eventCnt++;
           //}
       }


       return ShedCnt;
   }


   uint64_t ICDT_14_InputShedding(int _quota, uint64_t & _eventCnt)
   {
       int ToShedCnt = _quota;
       NormalEvent RawEvent;
       while(ToShedCnt> 0 && !RawEventQueue.empty())
       {
           int dice_roll = m_distribution(m_generator);

           RawEvent = RawEventQueue.front(); 
           if(RawEvent.name == "A" && dice_roll <= 90)
           {
               RawEventQueue.pop();
               --ToShedCnt;
           }
           else if(RawEvent.name == "B" && dice_roll > 90 && dice_roll <= 95)
           {
               RawEventQueue.pop();
               --ToShedCnt;
           }
           else if(RawEvent.name == "C" && dice_roll > 95)
           {
               RawEventQueue.pop();
               --ToShedCnt;
           }
           else
           {
               processEvent();
               _eventCnt++;
           }
       }

       return _quota - ToShedCnt;
   }

   uint64_t cost_based_InputShedding(int _quota, uint64_t & _eventCnt)
   {
      // cout << "[cost_based_InputShedding] " << endl;
       int ToShedCnt = _quota;
       NormalEvent RawEvent;
       while(ToShedCnt> 0 && !RawEventQueue.empty())
       {
           RawEvent = RawEventQueue.front();
           int dice_roll = m_distribution(m_generator);
           if(RawEvent.name == "A" && ( (RawEvent.v1 <38 || RawEvent.v1 > 50) || ( ((RawEvent.v1 < 42 && RawEvent.v1 > 37) || RawEvent.v1==50) && dice_roll >= 50 ) ) )
           {
               RawEventQueue.pop();
               --ToShedCnt;
           }
           else if(RawEvent.name == "B" && ( RawEvent.v2 <= 33 || RawEvent.v2 > 53))
           {
               RawEventQueue.pop();
               --ToShedCnt;
           }
           else 
           {
               processEvent();
               _eventCnt++;
           }
       }

      // cout << "[cost_based_InputShedding] succ leave shedding cnt: " << _quota - ToShedCnt << endl;
       return _quota - ToShedCnt;
       
   }

    


	void update_miner()
	{
		if (!m_Miner)
			return;

		string eqlFilename = m_MiningPrefix + ".eql";
		string scriptFilename = m_MiningPrefix + ".sh";

		ofstream script(scriptFilename, ofstream::out | ofstream::trunc);

		QueryLoader dst = m_Definition;
		for (size_t i = 0; i < m_Query->events.size() - 1; ++i)
		{
			m_Miner->printResult(i * 2, i * 2 + 1);

			auto options = m_Miner->generateResult(i * 2, i * 2 + 1);
			for (auto& it : options)
			{
				Query q = m_Miner->buildPredicateQuery(dst, *m_Query, (uint32_t)i, it);

				ostringstream namestr;
				namestr << q.name << "_mined_" << i << '_' << it.eventId;
				for (size_t i = 0; i < it.numAttr; ++i)
					namestr << '_' << it.attrIdx[i];

				q.name = namestr.str();

				script << "$CEP_CMD -c " << eqlFilename << " -q " << q.name << " < $CEP_IN > ${CEP_OUT}" << q.name << " &" << endl;
				dst.addQuery(move(q));
			}
		}

		dst.storeFile(eqlFilename.c_str());
	}

//protected:

    bool readEventStreamFromFiles(string file, int PMABUpperBound, bool _override)
    {
        //cout << "**********************!!!!!!!!!!!!!!!!!!in readEventStreamFromFiles" << endl;
        ifstream ifs;
        ifs.open(file.c_str());
        if( !ifs.is_open())
        {
            cout << "can't open file " << file << endl;
            return false;
        }


        string line;





        uniform_int_distribution<int> EventC_distribution(2,PMABUpperBound); 
        while(getline(ifs,line))
        {
            vector<string> dataEvent;
            stringstream lineStream(line);
            string cell;


            while(getline(lineStream,cell,','))
                dataEvent.push_back(cell);

            NormalEvent RawEvent;
            RawEvent.name = dataEvent[0];
            RawEvent.ArrivalQTime = stoi(dataEvent[1]);
            RawEvent.ID = stoi(dataEvent[2]); 
            RawEvent.v1 = stoi(dataEvent[3]);
            RawEvent.v2 = stoi(dataEvent[4]);

            if(_override && RawEvent.name == "C")
            {
                int Cv1 = EventC_distribution(m_generator);
                RawEvent.v1 = Cv1;
            }

            RawEventQueue.push(RawEvent);
            //cout << "added an event" << endl;
        }

        return true;

    }

    void dumpLatencyBooking(string file)
    {
        ofstream outFile;
        outFile.open(file.c_str());

        for(auto iter : m_Latency_booking)
            outFile << iter.first << "," << iter.second <<  endl;
    }

	void write_event(bool _timeout, uint32_t _state, const attr_t* _attributes)
	{
		StreamEvent r;
		r.typeIndex = m_ResultEventType;
		r.typeHash = m_ResultEventTypeHash;
		r.attributeCount = m_ResultAttributeCount;
		r.flags = 0;

		if (_timeout)
		{
			r.flags = StreamEvent::F_TIMEOUT;
			r.timeoutState = (uint8_t)(_state - 1);
		}

		if (m_AppendTimestamp)
		{
			r.flags |= StreamEvent::F_TIMESTAMP;
			r.attributes[r.attributeCount++] = current_utime();
		}

		uint64_t* outattr_it = r.attributes;
		for (auto it : m_OutEventAttrSrc)
			*outattr_it++ = _attributes[it];
       // cout << "0" << "," << flush;
	   // for (auto it : m_OutEventAttrSrc)
	   // 	cout << _attributes[it] << "," << flush;
       // cout << endl;
       


        //cout << "latency  in write event" << _attributes[Query::DA_FULL_MATCH_TIME] << "--" <<   _attributes[Query::DA_CURRENT_TIME] << "--" << r.attributes[Query::DA_CURRENT_TIME] << endl; 

		//r.write();

        //monitoring load shedding: if latency exceeds threshold for 2 times, call load shedding
        //monitor every 50,000 us, taking average latency in this time span
        //
        //===========================================
        //monitor latency per full match 
        //
        uint64_t la = _attributes[Query::DA_FULL_MATCH_TIME] - _attributes[Query::DA_CURRENT_TIME]; 
        m_RealTimeLatency = la;
        m_Latency_booking[la]++;
        //m_Latency_booking_Q.push(la);
        //
        //cout << la << endl;
        //cout << "[write_event] latency " << la << endl;
        ACCLantency += la;

       // if(la > 0.9*LATENCY)
       // {
       //     //cout << "[write_event] warning : lantancy " << la << " > " << 0.9*LATENCY << endl;
            if(la > LATENCY)
                ++NumHighLatency;

       //     if( _attributes[Query::DA_FULL_MATCH_TIME] - lastSheddingTime > 10000)
       //     {
       //         cout << "[write_event] shedding time span " << _attributes[Query::DA_FULL_MATCH_TIME] - lastSheddingTime << endl; 
       //         double LOF = (la-0.9*LATENCY)/(LATENCY);
       //         cout << "[write_event] LOF" << LOF << endl;
       //         if(LOF > 1)
       //             LOF=0.9;
       //         m_SheddingCnt += m_Matcher.approximate_BKP_PMshedding(LOF);
       //         lastSheddingTime = _attributes[Query::DA_FULL_MATCH_TIME];
       //         cout << "[write_event] finished " << endl;
       //     }
          //}
        //Trigger of load shedding
        // Here to compute how much and which partial matches or event to shed.
        //=============================================

        //=================================================================================================
        // monitor latency every 50ms.
        //
        //if(_attributes[Query::DA_FULL_MATCH_TIME] / 50000 == time / 50000)
        //{
        //    acctime += _attributes[Query::DA_FULL_MATCH_TIME];
        //    latency += la;
        //    Rtime = time/50000; //number R 50,000us
        //    cntFullMatch++;
        //}
        //else
        //{
        //    //print out latency
        //    //cout << Rtime << "," << long(latency/cntFullMatch) << endl;

        //    //printStatesSize();
        //    //printContribution();

        //    //==== for state-based shedding monitoring
        //    //======================================================
        //    if(latency/cntFullMatch > LATENCY) 
        //    {
        //        ++NumHighLatency;

        //        //for random input shedding =====
        //    //    if(m_InputSeddingSwitcher)
        //    //        m_RandomInputSheddingFlag = true;
        //    //    else
        //    //        m_RandomInputSheddingFlag = false;
        //    //
        //    //    if(m_PMSheddingSwitcher && _attributes[Query::DA_FULL_MATCH_TIME] - lastSheddingTime > 3000000)
        //    //    {

        //    //        //m_Matcher.randomLoadShedding();
        //    //        //time_point<high_resolution_clock> t0 = high_resolution_clock::now();
        //    //        //m_Matcher.computeScores4LoadShedding();
        //    //        //===for state-based load shedding
        //    //        if(PatternMatcher::MonitoringLoad() == true){
        //    //           m_Matcher.computeScores4LoadShedding();
        //    //           m_Matcher.loadShedding();
        //    //           ++loadCnt;
        //    //        }

        //    //        //time_point<high_resolution_clock> t1 = high_resolution_clock::now();
        //    //        //cout << "load shedding time " << duration_cast<microseconds>(t1 - t0).count() << endl;
        //    //        lastSheddingTime = _attributes[Query::DA_FULL_MATCH_TIME];
        //    //    }
        //    }
        //    //======================================================

        //        cntFullMatch = 1;
        //        time = _attributes[Query::DA_FULL_MATCH_TIME];
        //        latency = la;
        //        acctime = time;
        //        Rtime = time/50000;
        //    
        //}
        //==========================================================================================================
        




		if (m_Miner)
		{
			if (_timeout)
			{
				size_t idx = (_state - 1) * 2 + 1;
				uint32_t eventA = (uint32_t)r.attributes[_state];
				uint32_t eventB = (uint32_t)m_DefAttrId;

				m_Miner->addMatch(eventA, eventB, idx);
			}
			else
			{
				for (uint32_t s = 1; s < _state; ++s)
				{
					size_t idx = (s - 1) * 2;
					uint32_t eventA = (uint32_t)r.attributes[s];
					uint32_t eventB = (uint32_t)r.attributes[s + 1];

					m_Miner->addMatch(eventA, eventB + 1, idx);
				}
			}
		}
	}

//private:
	QueryLoader			m_Definition;
	const Query*		m_Query;

	unique_ptr<PredicateMiner> m_Miner;
	string				m_MiningPrefix;
	PatternMatcher		m_Matcher;
	vector<uint32_t>	m_OutEventAttrSrc;

    //default_random_engine m_generator; 
    //uniform_int_distribution<int> m_distribution(1,5);

    bool                m_RandomInputSheddingFlag;
    bool                m_InputSeddingSwitcher;
    bool                m_PMSheddingSwitcher;
    
    bool                m_Monitoring_Latency = false;

	uint16_t			m_ResultEventType;
	uint32_t			m_ResultEventTypeHash;
	uint8_t				m_ResultAttributeCount;
	bool				m_GenerateTimeoutEvents;
	bool				m_AppendTimestamp;

	uint64_t			m_DefAttrId;
	uint64_t			m_DefAttrOffset;

    uint64_t            m_SheddingCnt = 0;

	uint64_t			m_NextMinerUpdateTime;
    uint64_t            lastSheddingTime = 0;

    uint64_t            time;
    uint64_t            latency;
    uint64_t            acctime;
    uint64_t            Rtime;
    uint64_t            cntFullMatch;
    uint64_t            m_RealTimeLatency = 0;
    

    //RingBuffer<NormalEvent>&  RawEventQueue;
    queue<NormalEvent>&  RawEventQueue;
    
    map<int,uint64_t>  m_Latency_booking;
    queue<int>  m_Latency_booking_Q;
    set<int> m_PM_Booking;
    set<int> m_PM_Booking2;
    bool m_type_C_Booking[21];
    bool m_type_C_Booking2[21];
    int loadCnt;
    uint64_t m_printFMpoint = 0;
    uint64_t m_FM = 0;
};


int max(int a, int b) { return (a > b) ? a : b; }

void knapSackSolver(int W, int wt[], int val[], int name[], int n, set<int> &keepingPMSet)
{   
    int i, w;  
    int K[n + 1][W + 1]; 
    for(int i=0; i<=n; ++i)
        for(int j=W; j<=W; ++j)
            K[i][j] = 6000;

    // Build table K[][] in bottom up manner 
    for (i = 0; i <= n; i++) {
        for (w = 0; w <= W; w++) {
            if (i == 0 || w == 0)
                K[i][w] = 0;
            else if (wt[i - 1] <= w)
                K[i][w] = max(val[i - 1] +
                        K[i - 1][w - wt[i - 1]], K[i - 1][w]);
            else 
                K[i][w] = K[i - 1][w];
        }
    }

    // stores the result of Knapsack 
    int res = K[n][W];


    w = W;
    for (i = n; i > 0 && res > 0; i--) {

        // either the result comes from the top 
        // (K[i-1][w]) or from (val[i-1] + K[i-1] 
        // [w-wt[i-1]]) as in Knapsack table. If 
        // it comes from the latter one/ it means 
        // the item is included.  
        if (res == K[i - 1][w])
            continue;        
        else { 

            // This item is included. 
            //printf("%d ", wt[i - 1]); 
            keepingPMSet.insert(name[i-1]);

            // Since this weight is included its 
            // value is deducted 
            res = res - val[i - 1]; 
            w = w - wt[i - 1]; 
        }
    } 
}                                     

//bool PatternMatcher::loadMonitoringFlag = false;
int main(int _argc, char* _argv[])
{
	init_utime();

	const char* deffile = "default.eql";
	const char* queryName = 0;
	const char* monitorFile = 0;
	const char* miningPrefix = 0;
	bool captureTimeouts = false;
	bool appendTimestamp = false;
    bool InputShedding = false;
    bool PMShedding = false;

    bool MonitoringCons_Contr_Flag = false; 

    bool Flag_ClusteringPMShedding = false;
    bool Flag_RandomPMShedding = false;
    bool GeneratePMsFlag = false;


    bool Flag_selectivityPMShedding = false;

    bool Flag_inputShedVLDB_03 = false;
    bool Flag_inputShedICDT_14 = false;
    bool Flag_inputShedRandom = false;
    bool inputShed_cost_based = false;

    bool DropIrrelevantOnly = false;



    for(int i=0; i<11; ++i)
    {
        A[i]  = 0;
        Ac[i] = 0;
        B[i]  = 0;
        Bc[i] = 0; 
    }

    for(int i=0; i<21; ++i)
        C[i] = 0;



    //CPyInstance PInstance;


    string partialMatchOutPutFilePrefix = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/SampleData/PM";
    string suffix = "none";
    //string streamFile = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/StreamLog1.csv";
    //string streamFile = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/Uniform_StreamLog_c_2-10_500K.csv";
    string streamFile = "/home/zhaobo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/Uniform_StreamLog_change_500K.csv";
    //string streamFile = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/Uniform_StreamLog_c_2-6_500K.csv";
    //string streamFile = "/home/zhaobo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/Uniform_StreamLog_c_10-11_500K.csv";

    //string streamFile = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/StreamLog_500K.csv";
    //string streamFile = "../../NormalEventStreamGen/StreamLog.csv";
    //string streamFile = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/NormalEventStreamGen/StreamLog_500K.csv";
    //
    //string path = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/python-clustering-classification";

    //cout << "[main] flag1 " << endl;

    //string chdir_cmd = string("sys.path.append(\"") + path + "\")";
    //const char* cstr_cmd = chdir_cmd.c_str();
    //cout << "[main] flag2 " << endl;

    //PyRun_SimpleString("import sys");

    //cout << "[main] flag3 " << endl;
    //PyRun_SimpleString(cstr_cmd);

    
    string _pFile = string("DS4PM");
    string _pFunc = string("DS1");
    double sheddingRatio = 0;

    int PMABUpperBound = 10;
    int TTL = 0;

	int c;
	while ((c = _free_getopt(_argc, _argv, "c:q:p:m:n:r:D:T:tsIPRGMVzhaoO")) != -1)
	{
		switch (c)
		{
		case 'c':
			deffile = _free_optarg;
			break;
		case 'q':
			queryName = _free_optarg;
			break;
		case 'p':
			monitorFile = _free_optarg;
			break;
		case 'm':
			miningPrefix = _free_optarg;
			break;
        case 'n':
            suffix = string(_free_optarg);
            break;
        case 'r':
            sheddingRatio = stod(string(_free_optarg));
            break;
        case 'D':
            PMABUpperBound = stoi(string(_free_optarg));
            break;
        case 'T':
            TTL = stoi(string(_free_optarg));
            break;
		case 't':
			captureTimeouts = true;
			break;
		case 's':
			appendTimestamp = true;
            break;
        case 'I':
            InputShedding = true;
            break;
        case 'P':
            //PMShedding = true;
            Flag_ClusteringPMShedding = true;
			break;
        case 'R':
            Flag_RandomPMShedding = true;
            break;
        case 'G':
            GeneratePMsFlag = true;
            break;
        case 'M':
            MonitoringCons_Contr_Flag = true;
            break;
        case 'V':
            Flag_selectivityPMShedding = true;
            break;
        case 'z':
            Flag_inputShedVLDB_03 = true;
            break;
        case 'h':
            Flag_inputShedICDT_14= true;
            break;
        case 'a':
            Flag_inputShedRandom = true;
            break;
        case 'o':
            inputShed_cost_based = true;
            break;
        case 'O':
            DropIrrelevantOnly = true;
            break;

		default:
			abort();
		}
	}

    cout << suffix << endl;
    cout << sheddingRatio << endl;

    int ABUpperBound = 10-(sheddingRatio * 10);
    cout << ABUpperBound << endl;

    int CLowerBound = sheddingRatio*10;
    cout << CLowerBound << endl;

    //char p;
    //cin >> p;


    queue<NormalEvent> EventQueue;

    //NormalDistGen      StreamGenerator(EventQueue, 1000, 20000); 
    //StreamGenerator.run(45,5,//Distibution for A.v1
    //                    50,3,  // ... A.v2
    //                    40,5,  // ... B.v1
    //                    55,3,   // ... B.v2
    //                    50,3,  // ... C.v1
    //                    40,3);  // ... C.v2

    //StreamGenerator.stop();



	CepMatch prog(EventQueue);
	if (!prog.init(deffile, queryName, miningPrefix, captureTimeouts, appendTimestamp))
		return 1;

    //prog.readEventStreamFromFiles(streamFile, PMABUpperBound);
    prog.readEventStreamFromFiles(streamFile, PMABUpperBound, false); // don't change the distribution of event C

    prog.m_Matcher.setTTL(TTL);
    //string path = "/home/bo/CEP_load_shedding/src_PM_Distribution_test/python-clustering-classification";
    //string path = "../../python-clustering-classification";

    //cout << "[main] flag1 " << endl;

    //string chdir_cmd = string("sys.path.append(\"") + path + "\")";
    //const char* cstr_cmd = chdir_cmd.c_str();
    //cout << "[main] flag2 " << endl;

   // PyRun_SimpleString("import sys");

    //cout << "[main] flag3 " << endl;
   // PyRun_SimpleString(cstr_cmd);
    
    //cout << "[main] flag4 " << endl;
    

    //prog.m_Matcher.setPythonLearning(_pFile, _pFunc);
    //cout << "[main] flag5 " << endl;
    prog.m_Matcher.setTimeSliceSpan(prog.m_Query->within);


    prog.m_Matcher.m_States[0].stateBufferCount = 0;
    prog.m_Matcher.m_States[0].setTimesliceClusterAttributeCount(1,1,1);
    prog.m_Matcher.m_States[0].count[0][0] = 1;
    //set key attribute indexs
    //prog.m_Matcher.m_States[1].setKeyAttrIdx(1);
    //prog.m_Matcher.m_States[2].setKeyAttrIdx(4);

    //prog.m_Matcher.m_States[1].setTimesliceClusterAttributeCount(1,4,1);
    //prog.m_Matcher.m_States[2].setTimesliceClusterAttributeCount(1,4,1);

    prog.m_Matcher.m_States[1].setIndexAttribute(1);
    prog.m_Matcher.m_States[2].setIndexAttribute(4);


    prog.m_Matcher.m_States[1].addClusterAttrIdx(1);
    prog.m_Matcher.m_States[2].addClusterAttrIdx(1);
    prog.m_Matcher.m_States[2].addClusterAttrIdx(4);

    prog.m_Matcher.m_States[1].addKeyAttrIdx(1);
    prog.m_Matcher.m_States[2].addKeyAttrIdx(1);
    prog.m_Matcher.m_States[2].addKeyAttrIdx(4);


    //prog.m_Definition.print();
    //prog.m_Matcher.print();
    //
    //
    prog.m_Matcher.addClusterTag(1,0,0,234,104607);
    prog.m_Matcher.addClusterTag(1,0,1,3.3333333333,4046.625);
    prog.m_Matcher.addClusterTag(1,0,2,432,157341.333333);
    prog.m_Matcher.addClusterTag(1,0,3,92.6666666667,48944.5);
    
    prog.m_Matcher.addClusterTag(2,0,0,7.685873606,413.494423792);
    prog.m_Matcher.addClusterTag(2,0,1,1.9649122807,10344.7017544);
    prog.m_Matcher.addClusterTag(2,0,2,0,17537.0285714);
    prog.m_Matcher.addClusterTag(2,0,3,1.7356321839,4548.56321839);

    prog.m_Matcher.sortClusterTag();

    /*
     * manually set predicates a.v+b.v == c.v
     */
    PatternMatcher::Condition  _condition;
    _condition.param[0] = 1;
    _condition.param[1] = 4;
    _condition.param[2] = 1;  // c.v attribute index.
    _condition.constant = 0;
    _condition.op = PatternMatcher::OP_ADD;
    _condition.op2 = PatternMatcher::OP_EQUAL;


    //extract the last transition instance, which should be a.v+b.v=c.v. Then update the check condition and execution handler 
    PatternMatcher::Transition& t = prog.m_Matcher.m_Transitions.back();
     t.conditions.push_back(_condition);
     t.updateHandler(prog.m_Matcher.m_States[t.from], prog.m_Matcher.m_States[t.to]);



    prog.m_Matcher.m_States[2].timePointIdx = 3;
    prog.m_Matcher.m_States[1].timePointIdx = 0;
    prog.m_Matcher.m_States[3].timePointIdx = 5;

//    prog.m_Definition.print();
//    prog.m_Matcher.print();

    //char _ch;
    //cin >> _ch;


    
    //cout << "[main] flag6 " << endl;
    if(InputShedding)
    {
        cout << "Inpute shedding on " << endl;
        prog.InputSheddingOn();
    }
    else
        prog.InputSheddingOff();
    //cout << "[main] flag7 " << endl;

    if(PMShedding)
    {
        cout << "PM Shedding on " << endl;
        prog.PMSheddingOn();
        PatternMatcher::setMonitoringLoadOn();
    }
    else
    {
        PatternMatcher::setMonitoringLoadOff();
        prog.PMSheddingOff();
    }

	volatile uint64_t eventCounter = 0;
	//uint64_t eventCounter = 0;

    set<int> PMBook;
    bool C_keep_book[21];
    for(int i=0; i<21; ++i)
        C_keep_book[21] = false;




	MonitorThread monitor;
	monitor.addValue(&current_utime);        //set monitoring time. every 1 second
	monitor.addValue(&eventCounter, true);   //write #event every second
//	monitor.addValue(&NumFullMatch, false);  //write #FullMatch till now
	//monitor.addValue(&NumHighLatency, true);//write #HighLatency till now
   // monitor.addValue(&NumFullMatch, true);   //write #Fullmatch every second
   // monitor.addValue(&ACCLantency,true);     //write #AccLantecy every second
   // monitor.addValue(&NumPartialMatch,true);     //write #PM every second
//    monitor.addValue(&NumPartialMatch,false);     //write accumulated #PM till now 
                                             //the AVG Latency every second =  #AccLantecy every second / #Fullmatch every second
                                             //

    //MonitorThread monitorLatency;

    
	if (monitorFile)
    {
		monitor.start(monitorFile);
        //string _file = "_latency.csv";
        //monitorLatency.start_monitoring_latency(_file, prog.m_Latency_booking_Q);
    }

    if(MonitoringCons_Contr_Flag)
        PatternMatcher::setMonitoringLoadOn();
    else
        PatternMatcher::setMonitoringLoadOff();


    /************** PM shedding *********************/
    //prog.m_Matcher.clustering_classification_PM_shedding_4_typeC(2,2,10);
    //prog.m_Matcher.clustering_classification_PM_shedding_4_typeC(1,1,10);

    if(Flag_ClusteringPMShedding)
    {
        //prog.m_Matcher.clustering_classification_PM_shedding_semantic(sheddingRatio);
        //PMSheddingCombo :
        // ---- 1 min
        // -----2 max
        prog.m_Matcher.clustering_classification_PM_shedding_semantic_setPMSheddingCombo(2);

        //********** building up knapsack to pick up shedding candidate

        //setting up the 1st knapsack
        cout << "!!!!!!!!!!!!!!!!!!!!!!!!" << endl;
        int val[19];  // PM C+  
        int wt[19];   // PM C- 
        int name[19]; // PM lookup table
        
        for(int i=0; i<19; ++i)
            name[i] = i+2;

        for(int i=0; i<19; ++i)
        {
            int pc = i+2;
            if(pc <= PMABUpperBound)
                val[i] = pc - 1;
            if(pc > PMABUpperBound)
                val[i] = 1;   // strictly, the value shoud be 0, but we only need a partial order. There fore, 1 is also fine.

            if(pc <= 11)
                wt[i] = pc -1;
            if(pc > 11)
                wt[i] = 20-pc+1;
        }

        int n = sizeof(val) / sizeof(val[0]); 

        int RatioToKeep = 100 - sheddingRatio*100;

        knapSackSolver(RatioToKeep, wt, val, name, n, prog.m_PM_Booking);


        //setting up the 2nd knapsack

        for(int i=0; i<19; ++i)
        {
            int pc = i+2;
            if(pc <= 11)
                val[i] = 1;
            if(pc > 11)
                val[i] = 10-(pc-11);   // strictly, the value shoud be 0, but we only need a partial order. There fore, 1 is also fine.

            if(pc <= 11)
                wt[i] = pc -1;
            if(pc > 11)
                wt[i] = 20-pc+1;
        }

        n = sizeof(val) / sizeof(val[0]); 
        knapSackSolver(RatioToKeep, wt, val, name, n, prog.m_PM_Booking2);



        for(auto &a : prog.m_type_C_Booking)
            a = false;

        cout << "m_PM_Booking " << endl;
        for(auto PMKey : prog.m_PM_Booking)
        {
            cout << PMKey << "--" << flush;
            
            prog.m_Matcher.m_States[2].PMKeepingBook[PMKey] = true;
            prog.m_type_C_Booking[PMKey] = true;
        }
        cout << endl;


        

        for(auto &a : prog.m_type_C_Booking2)
            a = false;


        cout << "m_PM_Booking2 " << endl;
        for(auto PMKey : prog.m_PM_Booking2)
        {
            cout << PMKey << "--" << flush;
            prog.m_Matcher.m_States[2].PMKeepingBook2[PMKey] = true;
            prog.m_type_C_Booking2[PMKey] = true;
        }
        cout << endl;


        cout << "Keeping ratio " << RatioToKeep << endl;

        
        for(int i=0; i<21; ++i)
            cout << i << "---" << prog.m_type_C_Booking[i] << "   " << flush;
       cout << endl;
        
    }

    if(DropIrrelevantOnly)
    {
        prog.m_Matcher.clustering_classification_PM_shedding_semantic_setPMSheddingCombo(2);
        
        for(int PMKey =2; PMKey<= PMABUpperBound; ++PMKey)
        {
            prog.m_PM_Booking.insert(PMKey);
            prog.m_Matcher.m_States[2].PMKeepingBook[PMKey] = true;
            prog.m_type_C_Booking[PMKey] = true;
            
        }

    }

    if(Flag_RandomPMShedding)
    {
        prog.m_Matcher.clustering_classification_PM_shedding_random(1,sheddingRatio);
        prog.m_Matcher.clustering_classification_PM_shedding_random(2,sheddingRatio);
    }

    if(Flag_selectivityPMShedding)
        prog.m_Matcher.clustering_classification_PM_shedding_selectivity(sheddingRatio);
    /************************************************/

    cout << "monitoring " << PatternMatcher::MonitoringLoad() << endl;
    cout << "[main] flag8 " << endl;


    uint64_t sheddingCnt = 0;
    uint64_t inputSheddingCnt = 0;

    //int loadCnt = 0;
    uint64_t eventCntFlag = eventCounter + 20000;
    time_point<high_resolution_clock> begin_time_point = high_resolution_clock::now();


     
	while(prog.processEvent(eventCounter))
    {
    uint64_t eventInputSheddingProcessCnt = 0;
    //    cout << "queue size " <<  EventQueue.size() << endl;
		eventCounter++;
        //if(eventCounter%1000 == 0)
        //    prog.m_Matcher.print();


        //***********PM additinal input shedding***********
       // prog.SmPMS_max_combo_additional_InputShedding(sheddingRatio, eventCounter);
       if(Flag_ClusteringPMShedding)
           inputSheddingCnt = prog.SmPMS_additional_InputShedding(sheddingRatio, eventCounter);

       if(DropIrrelevantOnly)
           inputSheddingCnt = prog.SmPMS_additional_InputShedding(sheddingRatio, eventCounter);

        

        /************** input shedding *********************/
        if(Flag_inputShedVLDB_03)
        {
            //inputSheddingCnt = prog.VLDB_03_InputShedding(1,ABUpperBound,1,ABUpperBound,CLowerBound, 10, eventCounter);
            inputSheddingCnt = prog.VLDB_03_InputShedding(sheddingRatio, eventCounter);
        }
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,2,1,2,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,3,1,3,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,4,1,4,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,5,1,5,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,6,1,6,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,7,1,7,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,8,1,8,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,9,1,9,eventCounter);
        //inputSheddingCnt = prog.VLDB_03_InputShedding(1,10,1,10,eventCounter);
        //inputSheddingCnt = prog.RandomInputShedding(0.75,eventCounter);
        if(Flag_inputShedRandom)
            inputSheddingCnt = prog.RandomInputShedding(sheddingRatio,eventCounter);
        if(Flag_inputShedICDT_14)
            inputSheddingCnt = prog.selectivity_InputShedding(sheddingRatio,eventCounter);
        /************************************************/

//        if(false && eventCounter > eventCntFlag)
//        {
//            eventCntFlag = eventCounter + 20000;
//            //if(bool PMshedding = false)
//            if(Flag_ClusteringPMShedding)
//            {
//                //for(auto && B : prog.m_Matcher.m_States[1].buffers[0][1])
//                //    B.clear();
//                //prog.m_Matcher.m_States[1].count[0][1]=0;
//                //prog.m_Matcher.m_States[1].firstMatchId[0][1]=0;
//
//            //    sheddingCnt += prog.m_Matcher.clustering_classification_PM_shedding(1,0,1);
//                //cout << "drop timeslice 0, cluster 1 in state 1" << endl; 
//
//                //for(auto && B : prog.m_Matcher.m_States[2].buffers[0][1])
//                //    B.clear();
//                //prog.m_Matcher.m_States[2].count[0][1]=0;
//                //prog.m_Matcher.m_States[2].firstMatchId[0][1]=0;
//
//            //    sheddingCnt += prog.m_Matcher.clustering_classification_PM_shedding(2,0,1);
//
//                //cout << "drop timeslice 0, cluster 1 in state 2" << endl; 
//
//                //for(auto && B : prog.m_Matcher.m_States[1].buffers[0][3])
//                //    B.clear();
//                //prog.m_Matcher.m_States[1].count[0][3]=0;
//                //prog.m_Matcher.m_States[1].firstMatchId[0][3]=0;
//
//            //    sheddingCnt += prog.m_Matcher.clustering_classification_PM_shedding(1,0,3);
//                //cout << "drop timeslice 0, cluster 3 in state 1" << endl; 
//
//                //for(auto && B : prog.m_Matcher.m_States[2].buffers[0][3])
//                //    B.clear();
//                //prog.m_Matcher.m_States[2].count[0][3]=0;
//                //prog.m_Matcher.m_States[2].firstMatchId[0][3]=0;
//
//                sheddingCnt += prog.m_Matcher.clustering_classification_PM_shedding(2,0,1);
//                //sheddingCnt += prog.m_Matcher.clustering_classification_PM_shedding(2,0,2);
//                //sheddingCnt += prog.m_Matcher.clustering_classification_PM_shedding(2,0,3);
//                //cout << "drop timeslice 0, cluster 3 in state 2" << endl; 
//
//                cout << "PMdropping , real time latency : " << prog.m_RealTimeLatency << endl;
//            }
//
//            //if(bool PMRandomShedding = true)
//            if(Flag_RandomPMShedding)
//            {
//                cout << "random shedding called " << endl;
//                sheddingCnt += prog.m_Matcher.clustering_classification_PM_random_shedding(2810945/5);
//            }
//
//
//            if(Flag_selectivityPMShedding)
//            {
//                //cout << "[PMshedding] VLDB16 " << endl; 
//                sheddingCnt += prog.m_Matcher.loadShedding_VLDB16(2810945/2, 1, 2);
//            }
//
//            if(Flag_inputShedVLDB_03)
//            {
//                inputSheddingCnt += prog.VLDB_03_InputShedding(2000,eventInputSheddingProcessCnt);
//            }
//        
//            if(Flag_inputShedICDT_14)
//            {
//                inputSheddingCnt += prog.ICDT_14_InputShedding(2000,eventInputSheddingProcessCnt);
//            }
//
//            if(Flag_inputShedRandom)
//            {
//                inputSheddingCnt += prog.RandomInputShedding(10000,eventInputSheddingProcessCnt);
//            }
//
//            if(inputShed_cost_based)
//            {
//                inputSheddingCnt += prog.cost_based_InputShedding(2000,eventInputSheddingProcessCnt);
//            }
//            eventCounter += eventInputSheddingProcessCnt;
//
//
//        }
            eventCounter += eventInputSheddingProcessCnt;

        //if(PMShedding && PatternMatcher::MonitoringLoad() == true)
        //{
        //    if(eventCounter  == 2000000)
        //    //if(NumFullMatch == 800000 )
        //    {
        //        PatternMatcher::setMonitoringLoadOff();
        //        cout << "call loadshedding" << endl;
        //        
        //        //time_point<high_resolution_clock> t0 = high_resolution_clock::now();
        //        prog.m_Matcher.computeScores4LoadShedding();
        //        //prog.m_Matcher.loadShedding();
        //        //time_point<high_resolution_clock> t1 = high_resolution_clock::now();
        //        //cout << "compute4score time load shedding time " << duration_cast<microseconds>(t1 - t0).count() << endl;
        //    }

        //    //if(eventCounter > 100000 && eventCounter % 100000 == 1)
        //    //{

        //    //    time_point<high_resolution_clock> t0 = high_resolution_clock::now();
        //    //    prog.m_Matcher.loadShedding();
        //    //    time_point<high_resolution_clock> t1 = high_resolution_clock::now();
        //    //    cout << "laod shedding time " << duration_cast<microseconds>(t1 - t0).count() << endl;
        //    //    ++loadCnt;
        //    //}
        //}
    }
    time_point<high_resolution_clock> end_time_point = high_resolution_clock::now();

    //StreamGenerator.stop();


        


	//prog.update_miner();

    string _file = string("latency_")+suffix+".csv";
    prog.dumpLatencyBooking(_file);
   // prog.printContribution();
    std::cout << "perform " << prog.loadCnt << "loadshedding" << endl;
    std::cout << "eventCn " << eventCounter << endl;
    std::cout << "#full Match " << NumFullMatch << endl;
    std::cout << "#partial Match " << NumPartialMatch << endl;

    std::cout << "#high latecny (> " << LATENCY << ") " << NumHighLatency<< endl;
    if(NumFullMatch)
        cout << "avg latency: " << (double) (ACCLantency/NumFullMatch) << endl; 
    std::cout << "m_RandomInputSheddingFlag: " <<prog.m_RandomInputSheddingFlag <<  endl;
    std::cout << "#shedding PM " << NumShedPartialMatch << endl;
    cout << "#approximate_BKP_PMshedding " << prog.m_SheddingCnt << endl;
    cout << "inputShedding number " << inputSheddingCnt << endl;
    cout << "exe time in ms: " << duration_cast<milliseconds>(end_time_point - begin_time_point).count() << endl;
    //cout << "acc " << (long double) NumFullMatch / (long double) (7800635);

//    cout << "contributions A: " << endl;
//    for(int i = 1; i<=10; ++i)
//        cout << "-------" << i << " === " << Ac[i] << endl; 
//
//    cout << "consumptions A: " << endl;
//    for(int i = 1; i<=10; ++i)
//        cout << "-------" << i << " === " << A[i] << endl; 
//
//    cout << "contributions B: " << endl;
//    for(int i = 1; i<=10; ++i)
//        cout << "-------" << i << " === " << Bc[i] << endl; 
//
//    cout << "consumptions B: " << endl;
//    for(int i = 1; i<=10; ++i)
//        cout << "-------" << i << " === " << B[i] << endl; 
//
//    cout << "contributions C: " << endl;
//    for(int i = 1; i<=10; ++i)
//        cout << "-------" << i << " === " << C[i] << endl; 
//    prog.m_Matcher.printContributions();
	return 0;
}
