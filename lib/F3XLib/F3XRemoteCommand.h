#ifndef F3XRemoteCommand_h
#define F3XRemoteCommand_h

enum class F3XRemoteCommandType { 
  SignalB,
  CmdCycleTestRequest,
  CmdCycleTestAnswer,
  CmdSetRadio,
  CmdRestartMC,
  ValBatB,
  BLineStateReq,
  BLineStateResp,
  Invalid,
  SignalA,
  RemoteSignalBuzz,
  RemoteSignalStateReq,
  RemoteSignalStateResp,
};

class F3XRemoteCommand
{
public:
  F3XRemoteCommand();
  void begin();
  void write(char aChar);
  void write(char* aData);
  void consume();
  boolean available();
  F3XRemoteCommandType getType();
  String* getArg(int8_t aIdx=-1);
  String* getBuffer();
  String* createCommand(F3XRemoteCommandType);
  String* createCommand(F3XRemoteCommandType, String );
protected:
  String myBuffer;
};

#endif
