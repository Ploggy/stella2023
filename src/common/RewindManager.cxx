//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2019 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include <cmath>

#include "OSystem.hxx"
#include "Serializer.hxx"
#include "StateManager.hxx"
#include "TIA.hxx"
#include "EventHandler.hxx"

#include "RewindManager.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
RewindManager::RewindManager(OSystem& system, StateManager& statemgr)
  : myOSystem(system),
    myStateManager(statemgr)
{
  setup();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RewindManager::setup()
{
  myStateSize = 0;
  myLastTimeMachineAdd = false;

  const string& prefix = myOSystem.settings().getBool("dev.settings") ? "dev." : "plr.";

  // Work around a bug in XCode 11.2 with -O0 and -O1
  const uInt32 maxBufSize = MAX_BUF_SIZE;

  // TODO - Add proper bounds checking (define constexpr variables for this)
  //        Use those bounds in DeveloperDialog too
  mySize = std::min<uInt32>(
      myOSystem.settings().getInt(prefix + "tm.size"), maxBufSize);
  if(mySize != myStateList.capacity())
    resize(mySize);

  myUncompressed = std::min<uInt32>(
      myOSystem.settings().getInt(prefix + "tm.uncompressed"), maxBufSize);

  myInterval = INTERVAL_CYCLES[0];
  for(int i = 0; i < NUM_INTERVALS; ++i)
    if(INT_SETTINGS[i] == myOSystem.settings().getString(prefix + "tm.interval"))
      myInterval = INTERVAL_CYCLES[i];

  myHorizon = HORIZON_CYCLES[NUM_HORIZONS-1];
  for(int i = 0; i < NUM_HORIZONS; ++i)
    if(HOR_SETTINGS[i] == myOSystem.settings().getString(prefix + "tm.horizon"))
      myHorizon = HORIZON_CYCLES[i];

  // calc interval growth factor for compression
  // this factor defines the backward horizon
  const double MAX_FACTOR = 1E8;
  double minFactor = 0, maxFactor = MAX_FACTOR;
  myFactor = 1;

  while(myUncompressed < mySize)
  {
    double interval = myInterval;
    double cycleSum = interval * (myUncompressed + 1);
    // calculate nextCycles factor
    myFactor = (minFactor + maxFactor) / 2;
    // horizon not reachable?
    if(myFactor == MAX_FACTOR)
      break;
    // sum up interval cycles (first state is not compressed)
    for(uInt32 i = myUncompressed + 1; i < mySize; ++i)
    {
      interval *= myFactor;
      cycleSum += interval;
    }
    double diff = cycleSum - myHorizon;

    // exit loop if result is close enough
    if(std::abs(diff) < myHorizon * 1E-5)
      break;
    // define new boundary
    if(cycleSum < myHorizon)
      minFactor = myFactor;
    else
      maxFactor = myFactor;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool RewindManager::addState(const string& message, bool timeMachine)
{
  // only check for Time Machine states, ignore for debugger
  if(timeMachine && myStateList.currentIsValid())
  {
    // check if the current state has the right interval from the last state
    RewindState& lastState = myStateList.current();
    uInt32 interval = myInterval;

    // adjust frame timed intervals to actual scanlines (vs 262)
    if(interval >= 76 * 262 && interval <= 76 * 262 * 30)
    {
      const uInt32 scanlines = std::max<uInt32>
        (myOSystem.console().tia().scanlinesLastFrame(), 240);

      interval = interval * scanlines / 262;
    }

    if(myOSystem.console().tia().cycles() - lastState.cycles < interval)
      return false;
  }

  // Remove all future states
  myStateList.removeToLast();

  // Make sure we never run out of space
  if(myStateList.full())
    compressStates();

  // Add new state at the end of the list (queue adds at end)
  // This updates the 'current' iterator inside the list
  myStateList.addLast();
  RewindState& state = myStateList.current();
  Serializer& s = state.data;

  s.rewind();  // rewind Serializer internal buffers
  if(myStateManager.saveState(s) && myOSystem.console().tia().saveDisplay(s))
  {
    myStateSize = std::max(myStateSize, uInt32(s.size()));
    state.message = message;
    state.cycles = myOSystem.console().tia().cycles();
    myLastTimeMachineAdd = timeMachine;
    return true;
  }
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 RewindManager::rewindStates(uInt32 numStates)
{
  uInt64 startCycles = myOSystem.console().tia().cycles();
  uInt32 i;
  string message;

  for(i = 0; i < numStates; ++i)
  {
    if(!atFirst())
    {
      if(!myLastTimeMachineAdd)
        // Set internal current iterator to previous state (back in time),
        // since we will now process this state...
        myStateList.moveToPrevious();
      else
        // ...except when the last state was added automatically,
        // because that already happened one interval before
        myLastTimeMachineAdd = false;

      RewindState& state = myStateList.current();
      Serializer& s = state.data;
      s.rewind();  // rewind Serializer internal buffers
    }
    else
      break;
  }

  if(i)
    // Load the current state and get the message string for the rewind
    message = loadState(startCycles, i);
  else
    message = "Rewind not possible";

  if(myOSystem.eventHandler().state() != EventHandlerState::TIMEMACHINE)
    myOSystem.frameBuffer().showMessage(message);
  return i;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 RewindManager::unwindStates(uInt32 numStates)
{
  uInt64 startCycles = myOSystem.console().tia().cycles();
  uInt32 i;
  string message;

  for(i = 0; i < numStates; ++i)
  {
    if(!atLast())
    {
      // Set internal current iterator to nextCycles state (forward in time),
      // since we will now process this state
      myStateList.moveToNext();

      RewindState& state = myStateList.current();
      Serializer& s = state.data;
      s.rewind();  // rewind Serializer internal buffers
    }
    else
      break;
  }

  if(i)
    // Load the current state and get the message string for the unwind
    message = loadState(startCycles, i);
  else
    message = "Unwind not possible";

  if(myOSystem.eventHandler().state() != EventHandlerState::TIMEMACHINE)
    myOSystem.frameBuffer().showMessage(message);
  return i;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 RewindManager::windStates(uInt32 numStates, bool unwind)
{
  if(unwind)
    return unwindStates(numStates);
  else
    return rewindStates(numStates);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string RewindManager::saveAllStates()
{
  if (getLastIdx() == 0)
    return "Nothing to save";

  try
  {
    ostringstream buf;
    buf << myOSystem.stateDir()
      << myOSystem.console().properties().get(PropType::Cart_Name)
      << ".sta";

    Serializer out(buf.str(), Serializer::Mode::ReadWriteTrunc);
    if (!out)
      return "Can't save to all states file";

    uInt32 curIdx = getCurrentIdx();
    rewindStates(MAX_BUF_SIZE);
    uInt32 numStates = uInt32(cyclesList().size());

    // Save header
    buf.str("");
    out.putString(STATE_HEADER);
    out.putShort(numStates);
    out.putInt(myStateSize);

    unique_ptr<uInt8[]> buffer = make_unique<uInt8[]>(myStateSize);
    for (uInt32 i = 0; i < numStates; ++i)
    {
      RewindState& state = myStateList.current();
      Serializer& s = state.data;
      // Rewind Serializer internal buffers
      s.rewind();
      // Save state
      s.getByteArray(buffer.get(), myStateSize);
      out.putByteArray(buffer.get(), myStateSize);
      out.putString(state.message);
      out.putLong(state.cycles);

      unwindStates(1);
    }
    // restore old state position
    rewindStates(numStates - curIdx);

    buf.str("");
    buf << "Saved " << numStates << " states";
    return buf.str();
  }
  catch (...)
  {
    return "Error saving all states";
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string RewindManager::loadAllStates()
{
  try
  {
    ostringstream buf;
    buf << myOSystem.stateDir()
      << myOSystem.console().properties().get(PropType::Cart_Name)
      << ".sta";

    // Make sure the file can be opened for reading
    Serializer in(buf.str(), Serializer::Mode::ReadOnly);
    if (!in)
      return "Can't load from all states file";

    clear();
    uInt32 numStates;

    // Load header
    buf.str("");
    // Check compatibility
    if (in.getString() != STATE_HEADER)
      return "Incompatible all states file";
    numStates = in.getShort();
    myStateSize = in.getInt();

    unique_ptr<uInt8[]> buffer = make_unique<uInt8[]>(myStateSize);
    for (uInt32 i = 0; i < numStates; ++i)
    {
      if (myStateList.full())
        compressStates();

      // Add new state at the end of the list (queue adds at end)
      // This updates the 'current' iterator inside the list
      myStateList.addLast();
      RewindState& state = myStateList.current();
      Serializer& s = state.data;
      // Rewind Serializer internal buffers
      s.rewind();

      // Fill new state with saved values
      in.getByteArray(buffer.get(), myStateSize);
      s.putByteArray(buffer.get(), myStateSize);
      state.message = in.getString();
      state.cycles = in.getLong();
    }

    // initialize current state (parameters ignored)
    loadState(0, 0);

    buf.str("");
    buf << "Loaded " << numStates << " states";
    return buf.str();
  }
  catch (...)
  {
    return "Error loading all states";
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void RewindManager::compressStates()
{
  double expectedCycles = myInterval * myFactor * (1 + myFactor);
  double maxError = 1.5;
  uInt32 idx = myStateList.size() - 2;
  // in case maxError is <= 1.5 remove first state by default:
  Common::LinkedObjectPool<RewindState>::const_iter removeIter = myStateList.first();
  /*if(myUncompressed < mySize)
    //  if compression is enabled, the first but one state is removed by default:
    removeIter++;*/

  // iterate from last but one to first but one
  for(auto it = myStateList.previous(myStateList.last()); it != myStateList.first(); --it)
  {
    if(idx < mySize - myUncompressed)
    {
      expectedCycles *= myFactor;

      uInt64 prevCycles = myStateList.previous(it)->cycles;
      uInt64 nextCycles = myStateList.next(it)->cycles;
      double error = expectedCycles / (nextCycles - prevCycles);

      if(error > maxError)
      {
        maxError = error;
        removeIter = it;
      }
    }
    --idx;
  }
   myStateList.remove(removeIter); // remove
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string RewindManager::loadState(Int64 startCycles, uInt32 numStates)
{
  RewindState& state = myStateList.current();
  Serializer& s = state.data;

  myStateManager.loadState(s);
  myOSystem.console().tia().loadDisplay(s);

  Int64 diff = startCycles - state.cycles;
  stringstream message;

  message << (diff >= 0 ? "Rewind" : "Unwind") << " " << getUnitString(diff);
  message << " [" << myStateList.currentIdx() << "/" << myStateList.size() << "]";

  // add optional message
  if(numStates == 1 && !state.message.empty())
    message << " (" << state.message << ")";

  return message.str();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string RewindManager::getUnitString(Int64 cycles)
{
  constexpr Int32 NTSC_FREQ = 1193182; // ~76*262*60
  constexpr Int32 PAL_FREQ  = 1182298; // ~76*312*50
  const Int32 scanlines = std::max<Int32>(
      myOSystem.console().tia().scanlinesLastFrame(), 240);
  const bool isNTSC = scanlines <= 287;
  const Int32 freq = isNTSC ? NTSC_FREQ : PAL_FREQ; // = cycles/second

  constexpr Int32 NUM_UNITS = 5;
  const std::array<string, NUM_UNITS> UNIT_NAMES = {
    "cycle", "scanline", "frame", "second", "minute"
  };
  const std::array<Int64, NUM_UNITS+1> UNIT_CYCLES = {
    1, 76, 76 * scanlines, freq, freq * 60, Int64(1) << 62
  };

  stringstream result;
  Int32 i;

  cycles = std::abs(cycles);

  for(i = 0; i < NUM_UNITS - 1; ++i)
  {
    // use the lower unit up to twice the nextCycles unit, except for an exact match of the nextCycles unit
    // TODO: does the latter make sense, e.g. for ROMs with changing scanlines?
    if(cycles == 0 || (cycles < UNIT_CYCLES[i + 1] * 2 && cycles % UNIT_CYCLES[i + 1] != 0))
      break;
  }
  result << cycles / UNIT_CYCLES[i] << " " << UNIT_NAMES[i];
  if(cycles / UNIT_CYCLES[i] != 1)
    result << "s";

  return result.str();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt64 RewindManager::getFirstCycles() const
{
  return !myStateList.empty() ? myStateList.first()->cycles : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt64 RewindManager::getCurrentCycles() const
{
  if(myStateList.currentIsValid())
    return myStateList.current().cycles;
  else
    return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt64 RewindManager::getLastCycles() const
{
  return !myStateList.empty() ? myStateList.last()->cycles : 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IntArray RewindManager::cyclesList() const
{
  IntArray arr;

  uInt64 firstCycle = getFirstCycles();
  for(auto it = myStateList.cbegin(); it != myStateList.cend(); ++it)
    arr.push_back(uInt32(it->cycles - firstCycle));

  return arr;
}
