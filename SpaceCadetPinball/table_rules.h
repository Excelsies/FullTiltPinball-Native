#pragma once
#include <utility>
#include <vector>

class TBall;
class TPinballTable;
class TPinballComponent;
enum class MessageCode;

// Per-table rules interface for the Full Tilt tables (null for Space Cadet). C++11 only, like the rest of the engine.
class TableRules
{
public:
	virtual ~TableRules() = default;
	virtual void MakeLinks(TPinballTable* table) = 0;
	virtual void ClearLinks() = 0;
	virtual void Handler(MessageCode code, TPinballComponent* component) = 0;
	virtual void TableControlHandler(MessageCode code) = 0;
	virtual void BackdoorKey(int key, bool isLeftFlip, bool isRightFlip) = 0;
	virtual void UnstuckBall(TBall& ball, int dt) = 0;
	virtual void TextEvent(int /*ftCode*/) {}
	virtual const char* GetText(int id) = 0;
	virtual const char* const* CheatNames(int& count) = 0;
	virtual void DumpState(std::vector<std::pair<const char*, int>>& /*out*/) {}
};
