#include "common.h"
#include <array>
#include <string>
#include <iostream>
#include <algorithm>

#include "libafanasy/msgclasses/mcgeneral.h"
#include "libafanasy/msgclasses/mctaskpos.h"
#include "libafanasy/job.h"
#include "libafanasy/taskexec.h"
#include "libafanasy/msgclasses/mctask.h"
#include "libafanasy/jobprogress.h"
#include "libafanasy/msgclasses/mcafnodes.h"
#include "libafqt/qenvironment.h"
#include "server/useraf.h"

#include <QtCore>
#include "RadiolocationService.h"
#include "state.hpp"

#include <boost/lexical_cast.hpp>

#undef max

using namespace afermer;

const std::string Time2strHMS( int time32, bool clamp);
int combine(int a, int b, int c);
unsigned short combine2(char a, char b, char c);
size_t combine(int a, int b, int c, int d);
void unpack(int combined, int *a, int *b, int *c);
void unpack(size_t combined, int *a, int *b, int *c, int *d);




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// RadiolocationService::RadiolocationService/////////////////////////////////////////////////////////////

RadiolocationService::Ptr RadiolocationService::m_single = NULL;

bool RadiolocationService::getJobsStatistic(QList<int> &o_stat)
{
    // TODO redo this testing variant
    o_stat.clear();
    std::map<size_t, int>::iterator it_start = m_tasks_per_day.begin();

    std::vector<int> lst;
    for (std::map<size_t, int>::iterator it=m_tasks_per_day.begin(); it!=m_tasks_per_day.end(); ++it)
            lst.push_back(it->first);

    std::sort (lst.begin(), lst.end());
    
    int start = lst[0];
    int end = lst[ lst.size() - 1 ];

    for(size_t i = start; i <= end ; ++i)
    {
        int val = 0;
        if( m_tasks_per_day.count( i ) > 0)
            val = m_tasks_per_day[i];
        o_stat.push_back(val);
    }
    return true;
}


RadiolocationService::RadiolocationService() 
    : subscribed_job(-1)
    , previos_show_all_users_job(true)
{
    m_station = RadiolocationStation::create();
    m_jobs = JobObjectsLorry::create();
    m_users = UserObjectsLorry::create();
    m_blades = BladeObjectsLorry::create();
}

RadiolocationService::~RadiolocationService() {  }


void RadiolocationService::getServerIPAddress(std::string& o_address)
{
    m_station->getServerIPAddress(o_address);
}

void RadiolocationService::getUserName(std::string& o_ret)
{
    m_station->getUserName(o_ret);
}

bool RadiolocationService::isConnected()
{
    return m_station->isConnected();
}

UserObjectsLorry::Ptr RadiolocationService::usersLorry()
{
    return m_users;
}

JobObjectsLorry::Ptr RadiolocationService::jobsLorry()
{
    return m_jobs;
}

BladeObjectsLorry::Ptr RadiolocationService::bladesLorry()
{
    return m_blades;
}

void RadiolocationService::exit()
{    
    delete m_station.get();
}

//////////////////////////////////// TASKS ///////////////////////////////////////////////////////////////
bool RadiolocationService::get(QList<TaskObject> &o_tasks, int i_index)
{
    if ( !m_jobs->contain(i_index))
        return false;
    
    o_tasks.clear();
    m_task_resources.clear();

    long long curtime = time(0);

    int job_id, block_id, x;
    unpack(i_index, &job_id, &block_id, &x);

    std::ostringstream ostr;

    ostr << "{\"get\":{\"type\":\"jobs\",\"mode\":\"full\",\"binary\":true,\"ids\":[" << job_id << "]}}";
    Waves::Ptr answer4 = m_station->push(ostr);

    ostr.str("");

    af::MCAfNodes mcNodes( answer4.get() );

    std::vector<af::Af*> * list = mcNodes.getList();
    if (int( list->size() )<1){
        return false;
    }
    af::Job * itemjob = (af::Job*)((*list)[0]);

    ostr << "{\"get\":{\"binary\":true,\"type\":\"jobs\",\"ids\":[" << job_id << "],\"mode\":\"progress\"}}";
    Waves::Ptr answer5 = m_station->push(ostr);


    if (subscribed_job != job_id)
    {
        if (subscribed_job != -1)
            m_station->addJobId( subscribed_job, false );
        m_station->addJobId( job_id, true );
        subscribed_job = job_id;
    }
   
    af::JobProgress progress(answer5.get());

    const af::BlockData * block = itemjob->getBlock(block_id);
    if (block == NULL)
    {
        std::cerr << "RadiolocationService::get(QList<TaskObject>): block is NULL : " << block_id << std::endl;
        return false;
    }
    
    for( int t = 0; t < block->getTasksNum(); t++)
    {
        boost::shared_ptr<af::TaskExec> taskexec( block->genTask( t ) );
        af::TaskProgress * taskprogress = progress.tp[block_id][t] ;

        int percent       = taskprogress->percent;
        long long time_task = curtime - taskprogress->time_start;

        TaskState::State status = TaskState::State::RUNNING;
        
        if( taskprogress->state & AFJOB::STATE_RUNNING_MASK)
        {
            if( percent      <   0 )      percent =   0;
            if( percent      > 100 )      percent = 100;
        }
        else
            percent = 0;
        
        if( taskprogress->state & AFJOB::STATE_DONE_MASK )
        { 
            percent = 100;
            time_task = taskprogress->time_done - taskprogress->time_start ;
            status = TaskState::State::DONE;
        }
        else if ( taskprogress->state & AFJOB::STATE_SKIPPED_MASK )
        {
            percent = 100;
            time_task = taskprogress->time_done - taskprogress->time_start ;
            status = TaskState::State::SKIPPED;
        }

        if( taskprogress->state & AFJOB::STATE_READY_MASK ) 
        {
            time_task = 0;
            percent = 0;
            status = TaskState::State::READY;
        }
        else if (taskprogress->state & AFJOB::STATE_ERROR_MASK)
        {
            time_task = 0;
            percent = 0;
            status = TaskState::State::ERROR;
        }

        QString frame = QString::number( taskexec->getFrameStart() );
        if ( taskexec->getFramesNum() > 1 )
        {
            frame += "-";
            frame += QString::number( taskexec->getFrameFinish() );
        }

        size_t hash = combine( job_id, block_id, t );

        std::map<size_t, TaskManager::Ptr >::iterator it = m_task_resources.find(hash);
        if (it != m_task_resources.end())
            m_task_resources.erase (it);

        TaskManager::Ptr task_container = TaskManager::create( taskexec->getCommand(), taskexec->getNumber() );

        m_task_resources.insert( std::pair<size_t, TaskManager::Ptr >(hash,task_container) );

        TaskObject o = TaskObject( hash
                            , frame
                            , percent
                            , QString::fromStdString( Time2strHMS(time_task, false) )
                            , status
                            , QString::fromStdString( taskexec->getName() )
                            , QString::fromStdString( taskprogress->hostname )
                        );
        o_tasks.push_back(o);

    }

    return true;
}

QList<int> RadiolocationService::getTasksRawTime(int i_index)
{
    QList<int> o_tasks;

    if (i_index == 0)
        return o_tasks;

    long long curtime = time(0);

    int job_id, block_id, x;
    unpack(i_index, &job_id, &block_id, &x);

    std::ostringstream ostr;

    ostr << "{\"get\":{\"type\":\"jobs\",\"mode\":\"full\",\"binary\":true,\"ids\":[" << job_id << "]}}";
    Waves::Ptr answer4 = m_station->push(ostr);

    ostr.str("");

    af::MCAfNodes mcNodes( answer4.get() );

    std::vector<af::Af*> * list = mcNodes.getList();
    if (int( list->size() )<1){
        return o_tasks;
    }
    af::Job * itemjob = (af::Job*)((*list)[0]);


    ostr << "{\"get\":{\"binary\":true,\"type\":\"jobs\",\"ids\":[" << job_id << "],\"mode\":\"progress\"}}";
    Waves::Ptr answer5 = m_station->push(ostr);

    af::JobProgress progress(answer5.get());

    const af::BlockData * block = itemjob->getBlock(block_id);


    for( int t = 0; t < block->getTasksNum(); t++)
    {
        boost::shared_ptr<af::TaskExec> taskexec( block->genTask( t ) );
        af::TaskProgress * taskprogress = progress.tp[block_id][t] ;

        long long time_task = curtime - taskprogress->time_start;

        if( taskprogress->state & AFJOB::STATE_DONE_MASK )
        {
            time_task = taskprogress->time_done - taskprogress->time_start ;
        }
        else if ( taskprogress->state & AFJOB::STATE_SKIPPED_MASK )
        {
            time_task = taskprogress->time_done - taskprogress->time_start ;
        }

        if( taskprogress->state & AFJOB::STATE_READY_MASK )
        {
            time_task = 0;
        }
        else if (taskprogress->state & AFJOB::STATE_ERROR_MASK)
        {
            time_task = 0;
        }

        o_tasks.push_back(time_task);
    }

    return o_tasks;
}

QList<QString> RadiolocationService::getTasksFrame(int i_index)
{
    QList<QString> o_tasks;

    int job_id, block_id, x;
    unpack(i_index, &job_id, &block_id, &x);

    std::ostringstream ostr;

    ostr << "{\"get\":{\"type\":\"jobs\",\"mode\":\"full\",\"binary\":true,\"ids\":[" << job_id << "]}}";
    Waves::Ptr answer4 = m_station->push(ostr);

    ostr.str("");

    af::MCAfNodes mcNodes( answer4.get() );

    std::vector<af::Af*> * list = mcNodes.getList();
    if (int( list->size() )<1){
        return o_tasks;
    }
    af::Job * itemjob = (af::Job*)((*list)[0]);

    const af::BlockData * block = itemjob->getBlock(block_id);

    for( int t = 0; t < block->getTasksNum(); t++)
    {
        boost::shared_ptr<af::TaskExec> taskexec( block->genTask( t ) );

        QString frame = QString::number( taskexec->getFrameStart() );
        if ( taskexec->getFramesNum() > 1 )
        {
            frame += "-";
            frame += QString::number( taskexec->getFrameFinish() );
        }

        o_tasks.push_back(frame);
    }

    return o_tasks;
}


void RadiolocationService::taskLog(QString& o_ret, int i_index) { tasksGetOut(o_ret, "log", i_index ); }
void RadiolocationService::taskErrorHosts(QString& o_ret, int i_index) { tasksGetOut(o_ret, "error_hosts" , i_index ); }
void RadiolocationService::taskStdOut(QString& o_ret, int i_index, TaskState::State state) 
{ 
    if ( !checkIndexInTaskResources(i_index) )
        return;
    
    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);

    m_station->getTaskOutput(o_ret, job_id, block_id, task_id, state);
}


bool RadiolocationService::taskSkip(int i_index) {  return tasksOperation("skip", i_index ); }
bool RadiolocationService::taskRestart(int i_index) {     return tasksOperation("restart", i_index ); }

bool RadiolocationService::tasksOperation( const std::string & i_type, int i_index)
{
    if ( !checkIndexInTaskResources(i_index) )
        return false;

    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);
    
    std::ostringstream str;
    af::jsonActionStart( str, "jobs", "", std::vector<int>( 1, job_id));
    str << ",\n\"operation\":{\n\"type\":\"" << i_type << '"';
    str << ",\n\"task_ids\":[" << task_id;
    str << "]},\n\"block_ids\":[" << block_id << ']';

    af::jsonActionFinish( str);
    m_station->push(str);

    return true;
}

bool RadiolocationService::checkIndexInTaskResources(int i_index)
{
    if ( m_task_resources.size() < 1)
    {
        return false;
    }

    std::map<size_t, TaskManager::Ptr >::iterator m_task_resources_it = m_task_resources.find(i_index);
    if (m_task_resources_it == m_task_resources.end())
    {
        std::cerr << "RadiolocationService:: index not found: " << i_index << std::endl;
        return false;
    }

    return true;
}


bool RadiolocationService::tasksGetOut(QString & o_ret, const std::string& i_mode, int i_index)
{
    if ( !checkIndexInTaskResources(i_index) )
        return false;
    
    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);

    int task_number;
    
    std::ostringstream ostr;
    ostr << "{\"get\":{\"type\":\"jobs\"";
    ostr << ",\"mode\":\"" << i_mode << "\"";
    ostr << ",\"ids\":[" << job_id << "]";
    ostr << ",\"block_ids\":[" << block_id << "]";
    ostr << ",\"task_ids\":[" << task_id << "]";
    ostr << ",\"number\":" << task_number;
    ostr << ",\"mon_id\":" << m_station->getId();
    ostr << ",\"binary\":true";
    ostr << "}}";

    Waves::Ptr answer = m_station->push( ostr );

    af::MCTask task(answer.get());

    size_t type = task.getType();
    switch ( type )
    {
        case af::MCTask::TOutput:
        {
            o_ret += QString::fromStdString( task.getOutput() );
            break;
        }
        case af::MCTask::TLog:
            o_ret += QString::fromStdString( task.getLog() );
            break;
        case af::MCTask::TErrorHosts:
            o_ret += QString::fromStdString( task.getErrorHosts() );
            break;
        default:
            std::cerr << "RadiolocationService::tasksGetOut Unknown type: " <<  af::MCTask::TNAMES[ answer->type() ] << i_index << std::endl;
    }

    return true;
}


void RadiolocationService::taskCommand(QString& o_ret, int i_index)
{
    if ( !checkIndexInTaskResources(i_index) )
        return;

    o_ret = QString::fromStdString( m_task_resources[i_index]->m_command );
}



void jobGroupShowErrorBlades(QString& o_ret, RadiolocationStation::Ptr station, int i_job_id)
{
    std::ostringstream str;
    RadiolocationStation::getItemInfo(str, "error_hosts","jobs", i_job_id);
    Waves::Ptr answer = station->push( str );

    RadiolocationStation::QStringFromMsg(o_ret, answer); 
}

//////////////////////////////////// JOBS ///////////////////////////////////////////////////////////////
int getAvalibleSlots(af::Render * i_render, int i_total_slots)
{
    std::list<af::TaskExec*> active_task_list = i_render->takeTasks();
    int busy_slots = 0;
    for( std::list<af::TaskExec*>::const_iterator it = active_task_list.begin(); it != active_task_list.end(); it++)
        busy_slots += (*it)->getCapacity();
    return i_total_slots - busy_slots;
}


void RadiolocationService::jobsUpdate(bool show_all_users_job)
{
    m_total_jobs = 0;
    m_total_running_jobs = 0;
    m_total_error_jobs = 0;
    m_total_done_jobs = 0;
    m_total_offline_jobs = 0;
    m_total_ready_jobs = 0;

    m_tasks_per_day.clear();
    std::ostringstream str;

    if (show_all_users_job != previos_show_all_users_job)
    {
        previos_show_all_users_job = show_all_users_job;
        m_jobs->clear();
    }

    if (show_all_users_job == true)
    {
        str << "{\"get\":{\"binary\":true,\"type\":\"jobs\"}}";
    }
    else
    {
        size_t user_id = m_station->getUserId();
        str << "{\"get\":{\"binary\":true,\"type\":\"jobs\",\"uids\":[" << user_id << "]}}";
    }

    Waves::Ptr answer4 =  m_station->push( str );

    af::MCAfNodes mcNodes( answer4.get() );
    std::vector<af::Af*> * list = mcNodes.getList();

    int count = int( list->size() );
    if (count<1){
        return;
    }

    long long curtime = time(0);

    for (int i = 0 ; i < count; ++i)
    {
        af::Job * itemjob = (af::Job*)((*list)[i]);

        str.str("");
        itemjob->v_jsonWrite(str,0);
        
        int64_t group_state = itemjob->getState();

        int block_count = itemjob->getBlocksNum();
        int job_id = itemjob->getId();

        // str.str("");
        // str << "{\"get\":{\"binary\":true,\"type\":\"jobs\",\"ids\":[" << job_id << "],\"mode\":\"progress\"}}";
        // Waves::Ptr answer5 = m_station->push( str );
       
        // af::JobProgress progress(answer5.get());

        std::map<std::string,std::string> folders = itemjob->getFolders();

        std::string output_folder("");
        std::map<std::string,std::string>::const_iterator it = folders.find("output");

        if( it != folders.end())
            output_folder = (*it).second;

        size_t time_wait = itemjob->getTimeWait();
        size_t time_started = itemjob->getTimeStarted();
        int time_created = itemjob->getTimeCreation();

        
        QDateTime task_day = QDateTime::fromTime_t(time_started);
        size_t int_task_day = task_day.date().dayOfYear();
        if( m_tasks_per_day.count( int_task_day ) > 0)
            m_tasks_per_day[int_task_day] += 1;
        else
            m_tasks_per_day[int_task_day] = 1;

        int priority = itemjob->getPriority();
        QString user_name = QString::fromStdString( itemjob->getUserName() );
        QString job_name = QString::fromStdString( itemjob->getName() );
        QString hosts_mask = QString::fromStdString( itemjob->getHostsMask() );
        QString time_creation = QDateTime::fromTime_t( time_created ).toString("dd/MM/yy hh:mm");

        QString error_blades("");
        jobGroupShowErrorBlades(error_blades, m_station, job_id);
        
        int job_percentage = 0;
        for( int b = 0; b < block_count; b++)
        {
            const af::BlockData * block = itemjob->getBlock(b);
            job_percentage += block->getProgressPercentage();
        }

        for( int block_num = 0; block_num < block_count; ++block_num)
        {
            size_t hash = combine( job_id, block_num, 0 );

            if (m_jobs->isDeleted(hash)) continue;

            const af::BlockData * block = itemjob->getBlock(block_num);
            
            QList<QString> blades;

            float percent=0;
            long long curtime = time(0);//block->getTasksNum()

            // for( int t = 0; t < block->getTasksNum(); t++)
            // {
            //     af::TaskProgress taskprogress = *(progress.tp[block_num][t]) ;

            //     if (taskprogress.state & AFJOB::STATE_RUNNING_MASK){
            //         blades.push_back( QString::fromStdString(taskprogress.hostname) );
            //     }
            // }

            // std::sort( blades.begin(), blades.end() );
            // blades.erase( std::unique( blades.begin(), blades.end() ), blades.end() );
            int blades_length = 0;//blades.size();

            int block_percentage = block->getProgressPercentage();

            int last_time_update = m_jobs->lastTimeUpdatePercent(hash, block_percentage, curtime);
            int block_state = block->getState();
            int time_delta = 0;
            QString approx_time = "-"; 

            if( time_started && (( block_state & AFJOB::STATE_DONE_MASK) == false))
            {
                int percentage = job_percentage / block_count;
                if(( percentage > 0 ) && ( percentage < 100 ))
                {
                    int sec_run = last_time_update - time_started;
                    int sec_all = sec_run * 100.0 / percentage;
                    int eta = sec_all - sec_run;
                    int time_delta = curtime - last_time_update;
                    eta -= time_delta;
                    if( eta > 0 )
                        approx_time =  QString::fromStdString( Time2strHMS( eta, false ) );
                }
            }

            if( time_wait > curtime )
                approx_time = QString::fromStdString(Time2strHMS( time_wait - curtime, false  ) );
            
            QString service = afqt::stoq( block->getService() );
            if (service=="postcmd") continue;
            if (service=="wakeonlan") continue;
            if (service=="events") continue;

            m_total_jobs++;

            JobState::State status = JobState::State::STARTED;

            size_t elapsed_time_counter = 0;

            if ( group_state & AFJOB::STATE_OFFLINE_MASK)
            {
                status = JobState::State::OFFLINE;
                approx_time="Stop";
            }
            else
            {
                if ( block_state & AFJOB::STATE_RUNNING_MASK )
                {
                    m_total_running_jobs++;
                    status = JobState::State::RUNNING;
                    elapsed_time_counter = curtime - time_started - time_wait;
                }
                else if ( block_state & AFJOB::STATE_DONE_MASK )
                {
                    m_total_done_jobs++;
                    status = JobState::State::DONE;
                    approx_time="Done";
                    elapsed_time_counter = block->getProgressTasksSumRunTime();
                }
                else if ( block_state & AFJOB::STATE_OFFLINE_MASK )
                {
                    m_total_offline_jobs++;
                    status = JobState::State::OFFLINE;
                    approx_time="Stopped";
                }
                else if (  block_state & AFJOB::STATE_ERROR_MASK )
                {
                    status = JobState::State::ERROR;
                    m_total_error_jobs++;
                    approx_time="Error";
                }
                else if (  block_state & AFJOB::STATE_READY_MASK )
                {
                    m_total_ready_jobs++;
                    status = JobState::State::READY;
                    approx_time="Waiting";
                }
            }


            int block_capacity = block->getCapacity();
            QString block_name = QString::fromStdString(  block->getName() );
            QString depends = QString::fromStdString( block->getDependMask() );
            QString time_elapsed = QString::fromStdString( Time2strHMS(elapsed_time_counter, false) ); // 

            QString user_color;
            m_users->getUserColor(user_name, user_color);

            std::string repr = str.str();

            m_jobs->insert(  user_name
                            ,status
                            ,time_creation
                            ,block_count
                            ,time_elapsed
                            ,hosts_mask
                            ,service
                            ,priority
                            ,block_capacity
                            ,block_percentage
                            ,job_name
                            ,hash
                            ,block_num
                            ,blades
                            ,block_name
                            ,job_id
                            ,blades_length
                            ,approx_time
                            ,error_blades
                            ,depends
                            ,QString::fromStdString(output_folder)
                            ,user_color
                            ,block->getProgressErrorHostsNum()
                            ,repr
                    ) ;
        } // for block_num
    }// for count

}

int RadiolocationService::totalJobs() { return m_total_jobs; }
int RadiolocationService::runningJobs() { return m_total_running_jobs; }
int RadiolocationService::errorJobs() { return m_total_error_jobs; }
int RadiolocationService::doneJobs() { return m_total_done_jobs; }
int RadiolocationService::offlineJobs() { return m_total_offline_jobs; }
int RadiolocationService::readyJobs() { return m_total_ready_jobs; }

bool RadiolocationService::deleteJobGroup(const QList<int>& i_indeces) 
{ 
    for (size_t i = 0; i < i_indeces.size(); ++i)
        m_jobs->erase( i_indeces[i] );
    m_jobs->markToDelete(i_indeces);
    return groupJobAction("delete", i_indeces);
}

bool RadiolocationService::pauseJob(const QList<int>& i_index)  { return groupJobAction("pause", i_index); }
bool RadiolocationService::startJob(const QList<int>& i_index)  { return groupJobAction("start", i_index); }
bool RadiolocationService::stopJob(const QList<int>& i_index)   { return groupJobAction("stop", i_index);  }
bool RadiolocationService::jobSkipJobs(const QList<int>& i_index)   { return blockAction("skip", i_index);  }
bool RadiolocationService::jobRestartErrors(const QList<int>& i_index)   { return groupJobAction("restart_errors" , i_index   );}
bool RadiolocationService::jobRestartRunning(const QList<int>& i_index)  { return blockAction("restart_running" , i_index  );}
bool RadiolocationService::jobRestartSkipped(const QList<int>& i_index)  { return blockAction("restart_skipped", i_index   );}
bool RadiolocationService::jobRestartDone(const QList<int>& i_index)     { return blockAction("restart_done", i_index      );}
bool RadiolocationService::jobResetErrorHosts(const QList<int>& i_index) { return blockAction("reset_error_hosts", i_index );}
bool RadiolocationService::jobRestartPause(const QList<int>& i_index)    { return groupJobAction("restart_pause", i_index     );}
bool RadiolocationService::restartJob(const QList<int>& i_index) {    return blockAction("restart", i_index); }


bool RadiolocationService::jobSetWaitTime(int i_index, const QString& i_waittime_str)
{
    static const QString format("yyyy.MM.dd HH:mm:ss");

    uint32_t waittime = QDateTime::fromString( i_waittime_str, format ).toTime_t();

    if( waittime == unsigned(-1) )
        return false;

    std::ostringstream str;
    str << waittime;
    setJobParameter(i_index, "time_wait", str.str(), true);
    return true;

}


QString RadiolocationService::jobGetHostMask(int i_index)
{
    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);

    std::ostringstream ostr;

    ostr << "{\"get\":{\"type\":\"jobs\",\"mode\":\"full\",\"ids\":[" << job_id << "]}}";
    Waves::Ptr answer = m_station->push( ostr );

    af::Job job( answer.get() );
    
    QString current = afqt::stoq( job.getHostsMask() );
    return current;
}


QString RadiolocationService::jobLog(int i_index)
{
    QString o_ret("");
    if (!m_jobs->contain(i_index))
        return o_ret;
    
    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);
    
    std::ostringstream str;
    RadiolocationStation::getItemInfo( str, "log", "jobs", job_id);
    Waves::Ptr answer = m_station->push( str );

    RadiolocationStation::QStringFromMsg(o_ret, answer);

    return o_ret;
}


bool RadiolocationService::blockAction(const std::string& i_action_name, const QList<int>& i_indeces)
{
    std::map< int, std::vector<int> > job_n_block_ids;
    std::map< int, std::vector<int> >::iterator it;
    
    std::vector<int>::iterator index_it;
    std::vector<int> indeces;

    for (size_t i = 0; i < i_indeces.size(); ++i)
    {
        int index = i_indeces[i];
        if (!m_jobs->contain(index))
            continue;

        index_it = find (indeces.begin(), indeces.end(), index);
        if ( index_it != indeces.end() )
            continue;
        indeces.push_back(index);

        int job_id, block_id, task_id;
        unpack(index, &job_id, &block_id, &task_id);

        it = job_n_block_ids.find(job_id);
        if (it != job_n_block_ids.end())
            job_n_block_ids[job_id].push_back(block_id);
        else
            job_n_block_ids.insert ( std::pair< int,std::vector<int> >( job_id, std::vector<int>( 1, block_id )) );
    }

    for (it = job_n_block_ids.begin(); it != job_n_block_ids.end(); ++it)
    {
        std::stringstream joined_id_blocks; 
        std::ostream_iterator<int> output_iterator(joined_id_blocks, ",");
        std::copy(it->second.begin(), it->second.end(), output_iterator);
        
        std::string jb = joined_id_blocks.str();
        std::ostringstream str;
        af::jsonActionStart( str, "jobs", "", std::vector<int>( 1,  it->first ) );
        str << ",\n\"block_ids\":[" << jb.substr(0, jb.size()-1) << ']';

        if( i_action_name == "skip" ||
            i_action_name == "restart" ||
            i_action_name == "restart_running" ||
            i_action_name == "restart_skipped" ||
            i_action_name == "restart_done" ||
            i_action_name == "reset_error_hosts" )
        {
            str << ",\n\"operation\":{\"type\":\"" << i_action_name << "\"}";
            af::jsonActionFinish( str );
            m_station->push(str);
        }
    }
   
    return true;
}

bool RadiolocationService::groupJobAction(const std::string& i_action_name, const QList<int>& i_indeces)
{
    std::vector<int>::iterator index_it;
    std::vector<int> jobs_ids;
    
    for (size_t i = 0; i < i_indeces.size(); ++i)
    {
        int index = i_indeces[i];

        index_it = find (jobs_ids.begin(), jobs_ids.end(), index);
        if ( index_it != jobs_ids.end() )
            continue;

        int job_id, block_id, task_id;
        unpack(index, &job_id, &block_id, &task_id);

        jobs_ids.push_back(job_id);
    }

    std::ostringstream str;
    af::jsonActionOperation( str, "jobs", i_action_name, "", jobs_ids );
    m_station->push(str);
    return true;
}


bool RadiolocationService::jobSetPriority(const QList<int>& i_indeces, int i_value )
{
    std::vector<int>::iterator index_it;
    std::vector<int> jobs_ids;
    
    for (size_t i = 0; i < i_indeces.size(); ++i)
    {
        int index = i_indeces[i];
        if (!m_jobs->contain(index))
            continue;

        index_it = find (jobs_ids.begin(), jobs_ids.end(), index);
        if ( index_it != jobs_ids.end() )
            continue;

        int job_id, block_id, task_id;
        unpack(index, &job_id, &block_id, &task_id);

        jobs_ids.push_back(job_id);
    }
    
    m_station->setParameter("jobs", jobs_ids, "priority", boost::lexical_cast<std::string>(i_value), false);

    return true;
}


bool RadiolocationService::jobSetHostMask(const QList<int>& i_indeces, const QString& i_mask)
{
    QRegExp rx( i_mask, Qt::CaseInsensitive );
    if( rx.isValid() == false )
        return false;

    std::vector<int>::iterator index_it;
    std::vector<int> jobs_ids;
    
    for (size_t i = 0; i < i_indeces.size(); ++i)
    {
        int index = i_indeces[i];
        if (!m_jobs->contain(index))
            continue;

        index_it = find (jobs_ids.begin(), jobs_ids.end(), index);
        if ( index_it != jobs_ids.end() )
            continue;


        int job_id, block_id, task_id;
        unpack(index, &job_id, &block_id, &task_id);

        jobs_ids.push_back(job_id);
    }
    
    m_station->setParameter("jobs", jobs_ids, "hosts_mask", afqt::qtos( i_mask ), true);

    return true;
}


QString RadiolocationService::jobOutputFolder(int i_index)
{
    QString o_ret("");

    if (!m_jobs->contain(i_index))
        return o_ret;
    
    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);

    o_ret = m_jobs->get(i_index)->m_output_folder;

    std::string folder = o_ret.toStdString();

    if (folder != "")
    {
#ifdef WINNT
    PROCESS_INFORMATION pinfo;

    af::launchProgram( &pinfo, "start", folder, NULL, NULL, NULL, NULL,
        CREATE_NEW_CONSOLE, true);

    CloseHandle( pinfo.hThread);
    CloseHandle( pinfo.hProcess);
#else
    af::launchProgram( "xdg-open " + folder, folder, NULL, NULL, NULL);
#endif

    }

    return o_ret;
}



QString RadiolocationService::jobShowErrorBlades(int i_index)
{
    QString o_ret("");

    if (!m_jobs->contain(i_index))
        return o_ret;

    int job_id, block_id, task_id;
    unpack(i_index, &job_id, &block_id, &task_id);
    
    jobGroupShowErrorBlades(o_ret, m_station, job_id);

    return o_ret;
}



//////////////////////////////////// BLADES ///////////////////////////////////////////////////////////////
bool RadiolocationService::setBladeService(int i_index, bool i_enable, const QString& i_service)
{
    std::ostringstream str;
    af::jsonActionOperationStart( str, "renders", "service", "", std::vector<int>( 1, i_index ));
    str << ",\n\"name\":\"" << afqt::qtos( i_service ) << "\"";
    str << ",\n\"enable\":" << ( i_enable ? "true": "false" );
    af::jsonActionOperationFinish( str);
    
    m_station->push(str);
    return true;
}


bool RadiolocationService::actLaunchCmd(int i_index, bool i_exit, const QString& i_cmd)
{
    std::ostringstream str;
    af::jsonActionOperationStart( str, "renders", "launch_cmd", "", std::vector<int>( 1, i_index ));
    str << ",\n\"cmd\":\"" << afqt::qtos( i_cmd ) << "\"";
    if( i_exit )
        str << ",\n\"exit\":true";
    af::jsonActionOperationFinish( str );
    
    m_station->push( str );
    return true;
}

void RadiolocationService::actUser(int i_index, const QString& i_val) { setBladeParameter(i_index, "user_name", i_val, true); }
void RadiolocationService::actCapacity(int i_index, const QString& i_val) { setBladeParameter(i_index, "capacity", i_val, false); }
void RadiolocationService::actMaxTasks(int i_index, const QString& i_val) { setBladeParameter(i_index, "max_tasks", i_val, false); }
void RadiolocationService::actNIMBY(int i_index)       { setBladeParameter(i_index, "NIMBY",  "true",  false); }
void RadiolocationService::actNimby(int i_index)       { setBladeParameter(i_index, "nimby",  "true",  false); }
void RadiolocationService::actFree(int i_index)        { setBladeParameter(i_index, "nimby",  "false", false); }
void RadiolocationService::actSetHidden(int i_index)   { setBladeParameter(i_index, "hidden", "true",  false); }
void RadiolocationService::actUnsetHidden(int i_index) { setBladeParameter(i_index, "hidden", "false", false); }
void RadiolocationService::actEjectTasks(int i_index)      { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "eject_tasks" );}
void RadiolocationService::actEjectNotMyTasks(int i_index) { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "eject_tasks_keep_my" );}
void RadiolocationService::actExit(int i_index)            { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "exit" );}
void RadiolocationService::actDelete(int i_index)          { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "delete" );}
void RadiolocationService::actReboot(int i_index)          { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "reboot" );}
void RadiolocationService::actShutdown(int i_index)        { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "shutdown" );}
void RadiolocationService::actWOLSleep(int i_index)        { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "wol_sleep" );}
void RadiolocationService::actWOLWake(int i_index)         { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "wol_wake" );  }
void RadiolocationService::actRestoreDefaults(int i_index) { m_station->setOperation("renders", std::vector<int>( 1, i_index ), "restore_defaults" ); }


void RadiolocationService::actTask(QString& ret, const std::string& i_mode, int i_index)
{
    std::ostringstream str;
    RadiolocationStation::getItemInfo( str, i_mode, "tasks", i_index );
    Waves::Ptr answer = m_station->push( str );
    RadiolocationStation::QStringFromMsg(ret, answer); 
}

void RadiolocationService::actRequestLog(int i_index, QString& o_val)
{
    actTask(o_val, "log", i_index);
}


void RadiolocationService::actRequestTasksLog(int i_index, QString& o_val)
{
    actTask(o_val, "tasks_log", i_index);
}

void RadiolocationService::actRequestInfo(int i_index, QString& o_val)
{
    actTask(o_val, "full", i_index);
}


bool RadiolocationService::setBladeParameter(int i_index, const QString& i_param_name, const QString& i_param_value, bool i_quoted)
{
    return m_station->setParameter( "renders", std::vector<int>( 1, i_index ), i_param_name.toStdString(), i_param_value.toStdString(),  i_quoted);
}


bool RadiolocationService::setJobParameter(int i_index, const std::string& i_param_name, const std::string& i_param_value, bool i_quoted)
{
    return m_station->setParameter( "jobs", std::vector<int>( 1, i_index ), i_param_name, i_param_value,  i_quoted);
}

void RadiolocationService::getJobDependencies(int index, QList<JobObject::Ptr> &o_job_dependencies)
{
    for (int i = 0; i < m_blade_indeces[index].size(); ++i)
    {
        JobObject::Ptr obj = m_jobs->get(m_blade_indeces[index][i]);
        if (obj != NULL)
            o_job_dependencies.push_back( obj );
    }
}

void RadiolocationService::bladesUpdate()
{
    std::ostringstream str;
    str << "{\"get\":{\"binary\":true, \"type\":\"renders\"}}";
    Waves::Ptr answer4 = m_station->push( str );
   
    af::MCAfNodes mcNodes( answer4.get() );

    std::vector<af::Af*> * list = mcNodes.getList();

    int count = int( list->size() );
    long long curtime = time(0);

    for (int i = 0 ; i < count; ++i)
    {
        af::Render * render = (af::Render*)((*list)[i]);

        af::HostRes hres = render->getHostRes() ;

        int cpubusy = hres.cpu_user + hres.cpu_nice + hres.cpu_system + hres.cpu_iowait + hres.cpu_irq + hres.cpu_softirq;

        int loadmem = 0;
        if (hres.mem_total_mb > 0)
        {
            div_t divresult = div (100* (hres.mem_total_mb - hres.mem_free_mb), hres.mem_total_mb);
            loadmem = divresult.quot;
        }

        std::string address = render->getAddress().v_generateInfoString();

        size_t hash = RotateValue::make_hash(address);
        RotateValue::Ptr rotate;
        m_it_rotate = m_rotate.find(hash);
        if (m_it_rotate != m_rotate.end())
        {
            rotate = m_rotate[hash];
        }
        else
        {
            rotate = RotateValue::create();
            m_rotate.insert ( std::pair<size_t,RotateValue::Ptr>(hash,rotate) );
        }
        rotate->addValue(loadmem);

        int loadnet = hres.net_recv_kbsec;

        af::Host host = render->getHost();
        QString os = (false == host.m_os.empty()) ? QString::fromStdString(host.m_os) : QString("Undefined");
        long long busy_time = render->getTimeLaunch();
        QString job_names;
        QList<int> job_hashes;
        int total_slots = render->getCapacity();
        int avalible_slots = RadiolocationStation::getAvalibleSlotsAndJobNames(render, total_slots, job_names, job_hashes);

        m_it_blades = m_blade_indeces.find(i);
        if (m_it_blades == m_blade_indeces.end())
            m_blade_indeces.insert( std::pair<int,QList<int> >(i,job_hashes) );
        else
            m_blade_indeces[i] = job_hashes;

        if (busy_time != 0)
            busy_time = curtime - busy_time;

        QString host_name = QString::fromStdString(render->getName());
        QString grp("Group1");
        QString working_time = QString::fromStdString( Time2strHMS(busy_time, false) );
        QString ip_address = QString::fromStdString(address);

        BladeState::State state = BladeState::State::READY;
        if (render->isOffline())
            state = BladeState::State::OFFLINE;
        else
        {
            if (render->isBusy())
                state = BladeState::State::BUSY;
            if (render->isNimby())
                state = BladeState::State::NIMBY;
            if (render->isNIMBY())
                state = BladeState::State::BIG_NIMBY;
            if (render->isPaused())
                state = BladeState::State::PAUSED;
            if (render->isDirty())
                state = BladeState::State::DIRTY;
        }

        m_blades->insert(host_name
                ,ip_address
                ,os
                ,grp
                ,working_time
                ,cpubusy
                ,rotate->getValues()
                ,loadnet
                ,state
                ,total_slots
                ,avalible_slots
                ,host.m_max_tasks
                ,host.m_capacity
                ,render->getId()
                ,job_names

                ,host.m_properties
                ,host.m_resources
                ,host.m_data
                ,hres.cpu_num
                ,hres.cpu_mhz
                ,hres.mem_total_mb
                ,hres.mem_free_mb
                ,hres.mem_cached_mb
                ,hres.mem_buffers_mb
                ,hres.swap_used_mb
                ,hres.hdd_total_gb
                ,hres.hdd_free_gb
                ,hres.hdd_rd_kbsec
                ,hres.hdd_wr_kbsec
                ,hres.hdd_busy
                ,hres.net_recv_kbsec
                ,hres.net_send_kbsec
                );

    } // for
}


/////////////////////// USERS ///////////////////////////////////////////


void RadiolocationService::usersUpdate()
{
    std::ostringstream ostr;

    ostr << "{\"get\":{\"binary\":true,\"type\":\"users\"}}";
    Waves::Ptr answer4 = m_station->push(ostr);

    ostr.str("");

    af::MCAfNodes mcNodes( answer4.get() );

    std::vector<af::Af*> * list = mcNodes.getList();
    if (int( list->size() )<1){
        return;
    }

    int count = int( list->size() );

    for (int i = 0 ; i < count; ++i)
    {
        UserAf* user = (UserAf*)((*list)[i]);

        int id = user->getId();
        std::string name = user->getName();
        std::string hostname = user->getHostName();
        int priority = user->getPriority();
        int jobs_size = user->getNumRunningJobs();
        int tasks_size = user->getRunningTasksNumber();

        m_users->insert( QString::fromStdString(name)
                ,   jobs_size
                ,   tasks_size
                ,   QString::fromStdString(hostname)
                ,   ""
                ,   priority
                ,   id
                );
    }

}
