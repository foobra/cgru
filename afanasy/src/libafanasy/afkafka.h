//
// Created by ligen on 10/11/22.
//

#pragma once

#include "environment.h"
#include "rdkafkacpp.h"
namespace af
{

class AfKafka
{
public:
	AfKafka();
	~AfKafka();

	inline RdKafka::Producer *getProducer() { return producer; }
	inline bool isSuccessInit() { return successInit; }
	static AfKafka &getInstance();
	void sendKafkaMessage(const std::string &key, const std::string &infoStr, const std::string &topic);

private:
	RdKafka::Producer *producer = nullptr;
	bool successInit = true;
};
} // namespace af