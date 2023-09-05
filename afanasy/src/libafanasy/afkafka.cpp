//
// Created by ligen on 10/11/22.
//
#include "afkafka.h"
#include "logger.h"

using namespace af;

AfKafka::AfKafka()
{
	if (af::Environment::isServer())
	{
		std::string brokers = af::Environment::getKafkaAddress();
		AF_LOG << "init kafka with brokers " << brokers;
		std::string errStr;
		RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

		if (conf->set("bootstrap.servers", brokers, errStr) != RdKafka::Conf::CONF_OK)
		{
			std::cerr << errStr << std::endl;
			successInit = false;
		}
		else
		{
			producer = RdKafka::Producer::create(conf, errStr);
			if (!producer)
			{
				std::cerr << "Failed to create producer: " << errStr << std::endl;
				successInit = false;
			}
		}

		delete conf;
	}
}

AfKafka::~AfKafka()
{
	if (producer)
		delete producer;
}

AfKafka *AfKafka::getInstance()
{
	static AfKafka *afKafka = new AfKafka();
	return afKafka;
}

void AfKafka::sendKafkaMessage(const std::string &key, const std::string &infoStr, const std::string &topic)
{
	if (!successInit)
		return;

retry:
	RdKafka::ErrorCode err = producer->produce(topic,
		RdKafka::Topic::PARTITION_UA, //随机分区
									  /* Make a copy of the value */
		RdKafka::Producer::RK_MSG_COPY /* Copy payload */,
		/* Value */
		const_cast<char *>(infoStr.c_str()), infoStr.size(), //需要写入的数据
															 /* Key */
		const_cast<char *>(key.c_str()), key.size(),
		/* Timestamp (defaults to current time) */
		0,
		/* Message headers, if any */
		NULL,
		/* Per-message opaque value passed to
		 * delivery report */
		NULL);

	if (err != RdKafka::ERR_NO_ERROR)
	{
		AF_LOG << "% Failed to produce to topic " << topic << ": " << RdKafka::err2str(err);

		if (err == RdKafka::ERR__QUEUE_FULL)
		{ //存放数据的队列已满，将执行等待
			producer->poll(1000);
			goto retry;
		}
	}
	else
	{
		AF_LOG << "% Enqueued message (" << infoStr.size() << " bytes) "
			   << "for topic " << topic;
	}

	producer->poll(0); //保持常态

	AF_LOG << "% Flushing final messages...";
	producer->flush(10 * 1000 /* wait for max 10 seconds */);

	// run = true ;   //之前因为没有加这个就报错了

	if (producer->outq_len() > 0) //发送数据
		AF_LOG << "% " << producer->outq_len() << " message(s) were not delivered";
}
