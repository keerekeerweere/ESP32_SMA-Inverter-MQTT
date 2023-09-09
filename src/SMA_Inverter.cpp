/* MIT License

Copyright (c) 2022 Lupo135
Copyright (c) 2023 darrylb123

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "SMA_Inverter.h"

int32_t  value32 = 0;
int64_t  value64 = 0;
uint64_t totalWh = 0;
uint64_t totalWh_prev = 0;
time_t   dateTime = 0;

const char btPin[] = {'0','0','0','0',0}; // BT pin Always 0000. (not login passcode!)

InverterData ESP32_SMA_Inverter::invData = InverterData();
DisplayData ESP32_SMA_Inverter::dispData = DisplayData();


bool ESP32_SMA_Inverter::begin(String localName, bool isMaster) {
  boolean bOk = false;
    bOk = serialBT.begin("ESP32toSMA", true);   // "true" creates this device as a BT Master.
    bOk &= serialBT.setPin(&btPin[0]); 
    return bOk;
}


bool ESP32_SMA_Inverter::connect(uint8_t remoteAddress[]) {
  bool bGotConnected = serialBT.connect(remoteAddress);
  btConnected = bGotConnected;
  return bGotConnected; 
}

bool ESP32_SMA_Inverter::disconnect() {
  bool bGotDisconnected = serialBT.disconnect();
  btConnected = false;
  return bGotDisconnected;
}

//serialBT.disconnect();

bool ESP32_SMA_Inverter::isValidSender(uint8_t expAddr[6], uint8_t isAddr[6]) {
  for (int i = 0; i < 6; i++)
    if ((isAddr[i] != expAddr[i]) && (expAddr[i] != 0xFF)) {
      logV("Shoud-Addr: %02X %02X %02X %02X %02X %02X\n   Is-Addr: %02X %02X %02X %02X %02X %02X\n",
        expAddr[5], expAddr[4], expAddr[3], expAddr[2], expAddr[1], expAddr[5],
         isAddr[5],  isAddr[4],  isAddr[3],  isAddr[2],  isAddr[1],  isAddr[5]);
        return false;
    }
  return true;
}

// ----------------------------------------------------------------------------------------------
//unsigned int readBtPacket(int index, unsigned int cmdcodetowait) {
E_RC ESP32_SMA_Inverter::getPacket(uint8_t expAddr[6], int wait4Command) {
  //extern BluetoothSerial serialBT;
  logV("getPacket cmd=0x%04x\n", wait4Command);
  //extern bool readTimeout;
  int index = 0;
  bool hasL2pckt = false;
  E_RC rc = E_OK; 
  L1Hdr *pL1Hdr = (L1Hdr *)&btrdBuf[0];
  int retries = 10;
  do {
    // read L1Hdr
    uint8_t rdCnt=0;
    for (rdCnt=0;rdCnt<18;rdCnt++) {
      btrdBuf[rdCnt]= BTgetByte();
      if (readTimeout)  break;
    }
    logD("L1 Rec=%d bytes pkL=0x%04x=%d Cmd=0x%04x\n",
        rdCnt, pL1Hdr->pkLength, pL1Hdr->pkLength, pL1Hdr->command);

    if (rdCnt<17) {
      logV("L1<18=%d bytes", rdCnt);
      #if (DEBUG_SMA > 2)
      HexDump(BTrdBuf, rdCnt, 10, 'R');
      #endif
      return E_NODATA;
    }
    // Validate L1 header
    if (!((btrdBuf[0] ^ btrdBuf[1] ^ btrdBuf[2]) == btrdBuf[3])) {
      logW("Wrong L1 CRC!!" );
    }

    if (pL1Hdr->pkLength > sizeof(L1Hdr)) { // more bytes to read
      for (rdCnt=18; rdCnt<pL1Hdr->pkLength; rdCnt++) {
        btrdBuf[rdCnt]= BTgetByte();
        if (readTimeout) break;
      }
      logV("L2 Rec=%d bytes", rdCnt-18);
      #if (DEBUG_SMA > 2)
      HexDump(BTrdBuf, rdCnt, 10, 'R');
      #endif

      //Check if data is coming from the right inverter
      if (isValidSender(expAddr, pL1Hdr->SourceAddr)) {
        rc = E_OK;

        logD("HasL2pckt: 0x7E?=0x%02X 0x656003FF?=0x%08X\n", btrdBuf[18], get_u32(btrdBuf+19));
        if ((hasL2pckt == 0) && (btrdBuf[18] == 0x7E) && (get_u32(btrdBuf+19) == 0x656003FF)) {
          hasL2pckt = true;
        }

        if (hasL2pckt) {
          //Copy BTrdBuf to pcktBuf
          bool escNext = false;

          for (int i=sizeof(L1Hdr); i<pL1Hdr->pkLength; i++) {
            pcktBuf[index] = btrdBuf[i];
            //Keep 1st byte raw unescaped 0x7E
            if (escNext == true) {
              pcktBuf[index] ^= 0x20;
              escNext = false;
              index++;
            } else {
              if (pcktBuf[index] == 0x7D)
                escNext = true; //Throw away the 0x7d byte
              else
                index++;
            }
            if (index >= MAX_PCKT_BUF_SIZE) {
              logE("pcktBuf overflow! (%d)\n", index);
            }
          }
          pcktBufPos = index;
        } else {  // no L2pckt
          memcpy(pcktBuf, btrdBuf, rdCnt);
          pcktBufPos = rdCnt;
        }
      } else { // isValidSender()
          rc = E_RETRY;
      }
    } else {  // L1 only
    #if (DEBUG_SMA > 2)
      HexDump(BTrdBuf, rdCnt, 10, 'R');
    #endif
      //Check if data is coming from the right inverter
      if (isValidSender(expAddr, pL1Hdr->SourceAddr)) {
          rc = E_OK;

          memcpy(pcktBuf, btrdBuf, rdCnt);
          pcktBufPos = rdCnt;
      } else { // isValidSender()
          rc = E_RETRY;
      }
    }
    if (btrdBuf[0] != '\x7e') { 
       serialBT.flush();
       logD("CommBuf[0]!=0x7e -> BT-flush");
    }
    if (retries-- <=0) { 
      logE("Packet retries exceeded");
      ESP.restart();
    }
  } while (((pL1Hdr->command != wait4Command) || ((rc == E_RETRY)) && (0xFF != wait4Command)) );

  if ((rc == E_OK) ) {
  #if (DEBUG_SMA > 1)
    logD("<<<====Rd Content of pcktBuf =======>>>");
    HexDump(pcktBuf, pcktBufPos, 10, 'P');
    logD("==>>>");
  #endif
  }

  if (pcktBufPos > pcktBufMax) {
    pcktBufMax = pcktBufPos;
    logD("pcktBufMax is now %d bytes\n", pcktBufMax);
  }

  return rc;
}

// *************************************************

void ESP32_SMA_Inverter::writePacketHeader(uint8_t *buf, const uint16_t control, const uint8_t *destaddress) {
  //extern uint16_t fcsChecksum;


    pcktBufPos = 0;

    fcsChecksum = 0xFFFF;
    buf[pcktBufPos++] = 0x7E;
    buf[pcktBufPos++] = 0;  //placeholder for len1
    buf[pcktBufPos++] = 0;  //placeholder for len2
    buf[pcktBufPos++] = 0;  //placeholder for checksum
    int i;
    for(i = 0; i < 6; i++) buf[pcktBufPos++] = espBTAddress[i];
    for(i = 0; i < 6; i++) buf[pcktBufPos++] = destaddress[i];

    buf[pcktBufPos++] = (uint8_t)(control & 0xFF);
    buf[pcktBufPos++] = (uint8_t)(control >> 8);
}


bool ESP32_SMA_Inverter::isCrcValid(uint8_t lb, uint8_t hb)
{
  bool bRet = false;
  
    if (((lb == 0x7E) || (hb == 0x7E) || (lb == 0x7D) || (hb == 0x7D)))
      bRet = false;
    else
      bRet = true;

  logD("isCrcValid at pcktBufPos: %d", bRet);
  return bRet;
}


uint32_t ESP32_SMA_Inverter::getattribute(uint8_t *pcktbuf)
{
    const int recordsize = 40;
    uint32_t tag=0, attribute=0, prevTag=0;
    for (int idx = 8; idx < recordsize; idx += 4)
    {      
        attribute = ((uint32_t)get_u32(pcktbuf + idx));
        tag = attribute & 0x00FFFFFF;
        if (tag == 0xFFFFFE) // count on prevTag to contain a value to succeed
            break;
        if ((attribute >> 24) == 1) //only take into account meaningfull values here 
            if (prevTag==0) prevTag = tag; //interested in pop of vector, so only (possible) first value is interesting here if anything was added at back
    }

    return prevTag;
}


// ***********************************************
E_RC ESP32_SMA_Inverter::getInverterDataCfl(uint32_t command, uint32_t first, uint32_t last) {
  //extern uint8_t sixff[6];
  do {
    pcktID++;
    writePacketHeader(pcktBuf, 0x01, sixff); //addr_unknown);
    //if (invData.SUSyID == SID_SB240)
    //writePacket(pcktBuf, 0x09, 0xE0, 0, invData.SUSyID, invData.Serial);
    //else
    writePacket(pcktBuf, 0x09, 0xA0, 0, invData.SUSyID, invData.Serial);
    write32(pcktBuf, command);
    write32(pcktBuf, first);
    write32(pcktBuf, last);
    writePacketTrailer(pcktBuf);
    writePacketLength(pcktBuf);

  } while (!isCrcValid(pcktBuf[pcktBufPos - 3], pcktBuf[pcktBufPos - 2]));

    BTsendPacket(pcktBuf);
    int string[3] = { 0,0,0 }; //String number count

    uint8_t pcktcount = 0;
    bool  validPcktID = false;
    do {
    do {
      invData.status = getPacket(invData.BTAddress, 0x0001);
   
      if (invData.status != E_OK) return invData.status;
      if (validateChecksum()) {
        if ((invData.status = (E_RC)get_u16(pcktBuf + 23)) != E_OK) {
          logD("Packet status: 0x%02X\n", invData.status);
          return invData.status;
        }
        // *** analyze received data ***
        pcktcount = get_u16(pcktBuf + 25);
        uint16_t rcvpcktID = get_u16(pcktBuf + 27) & 0x7FFF;
        if (pcktID == rcvpcktID) {
          if ((get_u16(pcktBuf + 15) == invData.SUSyID) 
            && (get_u32(pcktBuf + 17) == invData.Serial)) {
            validPcktID = true;
            value32 = 0;
            value64 = 0;
            uint16_t recordsize = 4 * ((uint32_t)pcktBuf[5] - 9) / (get_u32(pcktBuf + 37) - get_u32(pcktBuf + 33) + 1);
            logD("pcktID=0x%04x recsize=%d BufPos=%d pcktCnt=%04x", 
                            rcvpcktID,   recordsize, pcktBufPos, pcktcount);
            for (uint16_t ii = 41; ii < pcktBufPos - 3; ii += recordsize) {
              uint8_t *recptr = pcktBuf + ii;
              uint32_t code = get_u32(recptr);
              //LriDef lri = (LriDef)(code & 0x00FFFF00);
              uint16_t lri = (code & 0x00FFFF00) >> 8;
              uint32_t cls = code & 0xFF;
              uint8_t dataType = code >> 24;
              time_t datetime = (time_t)get_u32(recptr + 4);
              logV("lri=0x%04x cls=0x%08X dataType=0x%02x",lri, cls, dataType);
       
              if (recordsize == 16) {
                value64 = get_u64(recptr + 8);
                logV("value64=%d=0x%08x",value64, value64);
       
                  //if (is_NaN(value64) || is_NaN((uint64_t)value64)) value64 = 0;
              } else if ((dataType != 16) && (dataType != 8)) { // ((dataType != DT_STRING) && (dataType != DT_STATUS)) {
                value32 = get_u32(recptr + 16);
                if ( value32 < 0) value32 = 0;
                logV("value32=%d=0x%08x",value32, value32);
              }
              switch (lri) {
              case GridMsTotW: //SPOT_PACTOT
                  //This function gives us the time when the inverter was switched off
                  invData.LastTime = datetime;
                  invData.Pac = value32;
                  dispData.Pac = (float)value32;
                  //debug_watt("SPOT_PACTOT", value32, datetime);
                  printUnixTime(timeBuf, datetime);
                  logI("Pac %15.3f kW %x  GMT:%s \n", tokW(value32),value32, timeBuf);
                  break;
       
              case GridMsWphsA: //SPOT_PAC1
                  invData.Pmax = value32;
                  dispData.Pmax = tokW(value32);
                  //debug_watt("SPOT_PAC1", value32, datetime);
                  logI("Pmax %14.2f kW \n", tokW(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case GridMsPhVphsA: //SPOT_UAC1
                  invData.Uac[0] = value32;
                  dispData.Uac[0] = toVolt(value32);
                  //debug_volt("SPOT_UAC1", value32, datetime);
                  logI("UacA %15.2f V  \n", toVolt(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
              case GridMsPhVphsB: //SPOT_UAC2
                  invData.Uac[1] = value32;
                  dispData.Uac[1] = toVolt(value32);
                  //debug_volt("SPOT_UAC1", value32, datetime);
                  logI("UacB %15.2f V  \n", toVolt(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;     
                case GridMsPhVphsC: //SPOT_UAC2
                  invData.Uac[2] = value32;
                  dispData.Uac[2] = toVolt(value32);
                  //debug_volt("SPOT_UAC1", value32, datetime);
                  logI("UacC %15.2f V  \n", toVolt(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;       
              case GridMsAphsA_1: //SPOT_IAC1
              case GridMsAphsA:
                  invData.Iac[0] = value32;
                  dispData.Iac[0] = toAmp(value32);
                  //debug_amp("SPOT_IAC1", value32, datetime);
                  logI("IacA %15.2f A  \n", toAmp(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
              case GridMsAphsB_1: //SPOT_IAC1
              case GridMsAphsB:
                  invData.Iac[1] = value32;
                  dispData.Iac[1] = toAmp(value32);
                  //debug_amp("SPOT_IAC1", value32, datetime);
                  logI("IacB %15.2f A  \n", toAmp(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
              case GridMsAphsC_1: //SPOT_IAC1
              case GridMsAphsC:
                  invData.Iac[2] = value32;
                  dispData.Iac[2] = toAmp(value32);
                  //debug_amp("SPOT_IAC1", value32, datetime);
                  logI("IacB %15.2f A  \n", toAmp(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case GridMsHz: //SPOT_FREQ
                  invData.Freq = value32;
                  dispData.Freq = toHz(value32);
                  logI("Freq %14.2f Hz \n", toHz(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case DcMsWatt: //SPOT_PDC1 / SPOT_PDC2
                  invData.Wdc[string[0]] = value32;
                  dispData.Wdc[string[0]++] = tokW(value32);
                  logI("PDC %15.2f kW \n", tokW(value32));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case DcMsVol: //SPOT_UDC1 / SPOT_UDC2
                  logI("Udc %15.2f V (%d) \n", toVolt(value32),string[1]);
                  invData.Udc[string[1]] = value32;
                  dispData.Udc[string[1]++] = toVolt(value32);
                  
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case DcMsAmp: //SPOT_IDC1 / SPOT_IDC2
                  logI("Idc %15.2f A (%d)\n", toAmp(value32),string[2]);
                  invData.Idc[string[2]] = value32;
                  dispData.Idc[string[2]++] = toAmp(value32);

                  //printUnixTime(timeBuf, datetime);
                  /* if ((invData.Udc[0]!=0) && (invData.Idc[0] != 0))
                    invData.Eta = ((uint64_t)invData.Uac * (uint64_t)invData.Iac * 10000) /
                                    ((uint64_t)invData.Udc[0] * (uint64_t)invData.Idc[0] );
                  else invData.Eta = 0;
                  logI("Efficiency %8.2f %%\n", toPercent(invData.Eta)); */
                  break;
       
              case MeteringDyWhOut: //SPOT_ETODAY
                  //This function gives us the current inverter time
                  //invData.InverterDatetime = datetime;
                  invData.EToday = value64;
                  dispData.EToday = tokWh(value64);
                  //debug_kwh("SPOT_ETODAY", value64, datetime);
                  logI("E-Today %11.3f kWh\n", tokWh(value64));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case MeteringTotWhOut: //SPOT_ETOTAL
                  //In case SPOT_ETODAY missing, this function gives us inverter time (eg: SUNNY TRIPOWER 6.0)
                  //invData.InverterDatetime = datetime;
                  invData.ETotal = value64;
                  dispData.ETotal = tokWh(value64);
                  //debug_kwh("SPOT_ETOTAL", value64, datetime);
                  logI("E-Total %11.3f kWh\n", tokWh(value64));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case MeteringTotOpTms: //SPOT_OPERTM
                  invData.OperationTime = value64;
                  //debug_hour("SPOT_OPERTM", value64, datetime);
                  logI("OperTime  %7.3f h \n", toHour(value64));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case MeteringTotFeedTms: //SPOT_FEEDTM
                  invData.FeedInTime = value64;
                  //debug_hour("SPOT_FEEDTM", value64, datetime);
                  logI("FeedTime  %7.3f h  \n", toHour(value64));
                  //printUnixTime(timeBuf, datetime);
                  break;
       
              case CoolsysTmpNom:
                  invData.InvTemp = value32;
                  dispData.InvTemp = toTemp(value32);
                  logI("Temp.     %7.3f C \n", toTemp(value32));
                  break;
              case OperationHealth:
                  value32 = getattribute(recptr);
                  invData.DevStatus = value32;
                  logI("Device Status:    %d  \n", value32);
                  break;
              case OperationGriSwStt:
                  value32 = getattribute(recptr);
                  invData.GridRelay = value32;
                  logI("Grid Relay:    %d  \n", value32);
                  break;
              case MeteringGridMsTotWOut:
                  //invData.MeteringGridMsTotWOut = value32;
                  break;
              case MeteringGridMsTotWIn:
                  //invData.MeteringGridMsTotWIn = value32;
                  break;
              default:

                logI("Caught: %x %d\n",lri,value32);
              }
            } //for
          } else {
            logW("*** Wrong SUSyID=%04x=%04x Serial=%08x=%08x", 
                 get_u16(pcktBuf + 15), invData.SUSyID, get_u32(pcktBuf + 17),invData.Serial);
          }
        } else {  // wrong PacketID
          logW("PacketID mismatch: exp=0x%04X is=0x%04X\n", pcktID, rcvpcktID);
          validPcktID = false;
          pcktcount = 0;
        }
      } else { // invalid Checksum
        invData.status = E_CHKSUM;
        return invData.status;
      }
   } while (pcktcount > 0);
   } while (!validPcktID);
   return invData.status;
}
// ***********************************************
E_RC ESP32_SMA_Inverter::getInverterData(enum getInverterDataType type) {
  E_RC rc = E_OK;
  uint32_t command;
  uint32_t first;
  uint32_t last;

  switch (type) {
  case EnergyProduction:
      logD("*** EnergyProduction ***");
      // SPOT_ETODAY, SPOT_ETOTAL
      command = 0x54000200;
      first = 0x00260100;
      last = 0x002622FF;
      break;

  case SpotDCPower:
      logD("*** SpotDCPower ***");
      // SPOT_PDC1, SPOT_PDC2
      command = 0x53800200;
      first = 0x00251E00;
      last = 0x00251EFF;
      break;

  case SpotDCVoltage:
      logD("*** SpotDCVoltage ***");
      // SPOT_UDC1, SPOT_UDC2, SPOT_IDC1, SPOT_IDC2
      command = 0x53800200;
      first = 0x00451F00;
      last = 0x004521FF;
      break;

  case SpotACPower:
      logD("*** SpotACPower ***");
      // SPOT_PAC1, SPOT_PAC2, SPOT_PAC3
      command = 0x51000200;
      first = 0x00464000;
      last = 0x004642FF;
      break;

  case SpotACVoltage:
      logD("*** SpotACVoltage ***");
      // SPOT_UAC1, SPOT_UAC2, SPOT_UAC3, SPOT_IAC1, SPOT_IAC2, SPOT_IAC3
      command = 0x51000200;
      first = 0x00464800;
      last = 0x004655FF;
      break;

  case SpotGridFrequency:
      logD("*** SpotGridFrequency ***");
      // SPOT_FREQ
      command = 0x51000200;
      first = 0x00465700;
      last = 0x004657FF;
      break;

  case SpotACTotalPower:
      logD("*** SpotACTotalPower ***");
      // SPOT_PACTOT
      command = 0x51000200;
      first = 0x00263F00;
      last = 0x00263FFF;
      break;

  case TypeLabel:
      logD("*** TypeLabel ***");
      // INV_NAME, INV_TYPE, INV_CLASS
      command = 0x58000200;
      first = 0x00821E00;
      last = 0x008220FF;
      break;

  case SoftwareVersion:
      logD("*** SoftwareVersion ***");
      // INV_SWVERSION
      command = 0x58000200;
      first = 0x00823400;
      last = 0x008234FF;
      break;

  case DeviceStatus:
      logD("*** DeviceStatus ***");
      // INV_STATUS
      command = 0x51800200;
      first = 0x00214800;
      last = 0x002148FF;
      break;

  case GridRelayStatus:
      logD("*** GridRelayStatus ***");
      // INV_GRIDRELAY
      command = 0x51800200;
      first = 0x00416400;
      last = 0x004164FF;
      break;

  case OperationTime:
      logD("*** OperationTime ***");
      // SPOT_OPERTM, SPOT_FEEDTM
      command = 0x54000200;
      first = 0x00462E00;
      last = 0x00462FFF;
      break;

  case InverterTemp:
      logD("*** InverterTemp ***");
      command = 0x52000200;
      first = 0x00237700;
      last = 0x002377FF;
      break;

  case MeteringGridMsTotW:
      logD("*** MeteringGridMsTotW ***");
      command = 0x51000200;
      first = 0x00463600;
      last = 0x004637FF;
      break;

  default:
      logW("Invalid getInverterDataType!!");
      return E_BADARG;
  };

  // Request data from inverter
  for (uint8_t retries=1;; retries++) {
    rc = getInverterDataCfl(command, first, last);
    if (rc != E_OK) {
      if (retries>1) {
         return rc;
      }
      logI("Retrying.%d",retries);
    } else {
      break;
    } 
  }

  return rc;
}

//-------------------------------------------------------------------------
bool ESP32_SMA_Inverter::getBT_SignalStrength() {
  logI("*** SignalStrength ***");
  writePacketHeader(pcktBuf, 0x03, invData.BTAddress);
  writeByte(pcktBuf,0x05);
  writeByte(pcktBuf,0x00);
  writePacketLength(pcktBuf);
  BTsendPacket(pcktBuf);

  getPacket(invData.BTAddress, 4);
  dispData.BTSigStrength = ((float)btrdBuf[22] * 100.0f / 255.0f);
  logI("BT-Signal %9.1f %%", dispData.BTSigStrength );
  return true;
}

//-------------------------------------------------------------------------
E_RC ESP32_SMA_Inverter::initialiseSMAConnection() {
  //extern uint8_t sixff[6];
  logI(" -> Initialize");
  getPacket(invData.BTAddress, 2); // 1. Receive
  invData.NetID = pcktBuf[22];
  logI("SMA netID=%02X\n", invData.NetID);
  writePacketHeader(pcktBuf, 0x02, invData.BTAddress);
  write32(pcktBuf, 0x00700400);
  writeByte(pcktBuf, invData.NetID);
  write32(pcktBuf, 0);
  write32(pcktBuf, 1);
  writePacketLength(pcktBuf);

  BTsendPacket(pcktBuf);             // 1. Reply
  getPacket(invData.BTAddress, 5); // 2. Receive

  // Extract ESP32 BT address
  memcpy(espBTAddress, pcktBuf+26,6); 
  logW("ESP32 BT address: %02X:%02X:%02X:%02X:%02X:%02X\n",
             espBTAddress[5], espBTAddress[4], espBTAddress[3],
             espBTAddress[2], espBTAddress[1], espBTAddress[0]);

  pcktID++;
  writePacketHeader(pcktBuf, 0x01, sixff); //addr_unknown);
  writePacket(pcktBuf, 0x09, 0xA0, 0, 0xFFFF, 0xFFFFFFFF); // anySUSyID, anySerial);
  write32(pcktBuf, 0x00000200);
  write32(pcktBuf, 0);
  write32(pcktBuf, 0);
  writePacketTrailer(pcktBuf);
  writePacketLength(pcktBuf);

  BTsendPacket(pcktBuf);             // 2. Reply
  if (getPacket(invData.BTAddress, 1) != E_OK) // 3. Receive
    return E_INIT;

  if (!validateChecksum())
    return E_CHKSUM;

  invData.Serial = get_u32(pcktBuf + 57);
  logW("Serial Nr: %lu\n", invData.Serial);
  return E_OK;
}

// log off SMA Inverter - adapted from SBFspot Open Source Project (SBFspot.cpp) by mrtoy-me
void ESP32_SMA_Inverter::logoffSMAInverter()
{
  //extern uint8_t sixff[6];
  pcktID++;
  writePacketHeader(pcktBuf, 0x01, sixff);
  writePacket(pcktBuf, 0x08, 0xA0, 0x0300, 0xFFFF, 0xFFFFFFFF);
  write32(pcktBuf, 0xFFFD010E);
  write32(pcktBuf, 0xFFFFFFFF);
  writePacketTrailer(pcktBuf);
  writePacketLength(pcktBuf);
  BTsendPacket(pcktBuf);
  return;
}

// **** Logon SMA **********
E_RC ESP32_SMA_Inverter::logonSMAInverter(const char *password, const uint8_t user) {
  //extern uint8_t sixff[6];
  #define MAX_PWLENGTH 12
  uint8_t pw[MAX_PWLENGTH];
  E_RC rc = E_OK;

  // Encode password
  uint8_t encChar = (user == USERGROUP)? 0x88:0xBB;
  uint8_t idx;
  for (idx = 0; (password[idx] != 0) && (idx < sizeof(pw)); idx++)
    pw[idx] = password[idx] + encChar;
  for (; idx < MAX_PWLENGTH; idx++) pw[idx] = encChar;

    bool validPcktID = false;
    time_t now;
    pcktID++;
    now = time(NULL);
    writePacketHeader(pcktBuf, 0x01, sixff);
    writePacket(pcktBuf, 0x0E, 0xA0, 0x0100, 0xFFFF, 0xFFFFFFFF); // anySUSyID, anySerial);
    write32(pcktBuf, 0xFFFD040C);
    write32(pcktBuf, user); //userGroup);    // User / Installer
    write32(pcktBuf, 0x00000384); // Timeout = 900sec ?
    write32(pcktBuf, now);
    write32(pcktBuf, 0);
    writeArray(pcktBuf, pw, sizeof(pw));
    writePacketTrailer(pcktBuf);
    writePacketLength(pcktBuf);

    BTsendPacket(pcktBuf);
    if ((rc = getPacket(sixff, 1)) != E_OK) return rc;

    if (!validateChecksum()) return E_CHKSUM;
    uint16_t rcvpcktID = get_u16(pcktBuf+27) & 0x7FFF;
    if ((pcktID == rcvpcktID) && (get_u32(pcktBuf + 41) == now)) {
      invData.SUSyID = get_u16(pcktBuf + 15);
      invData.Serial = get_u32(pcktBuf + 17);
      logV("Set:->SUSyID=0x%02X ->Serial=0x%02X ", invData.SUSyID, invData.Serial);
      validPcktID = true;
      uint8_t retcode = get_u16(pcktBuf + 23);
      // switch (retcode) {
      //     case 0: rc = E_OK; break;
      //     case 0x0100: rc = E_INVPASSW; break;
      //     default: rc = (E_RC)retcode; break;
      // }
    } else { 
      logW("Unexpected response  %02X:%02X:%02X:%02X:%02X:%02X pcktID=0x%04X rcvpcktID=0x%04X now=0x%04X", 
                   btrdBuf[9], btrdBuf[8], btrdBuf[7], btrdBuf[6], btrdBuf[5], btrdBuf[4],
                   pcktID, rcvpcktID, now);
      rc = E_INVRESP;
    }
    return rc;
}

/* 
// ******* Archive Day Data **********
E_RC ArchiveDayData(time_t startTime) {
  DEBUG2_PRINT("*** ArchiveDayData ***");
  printUnixTime(timeBuf, startTime); DEBUG2_PRINTF("\nStartTime0 GMT:%s", timeBuf);
  // set time to begin of day
  uint8_t minutes = (startTime/60) % 60;
  uint8_t hours = (startTime/(60*60)) % 24;
  startTime -= minutes*60 + hours*60*60;
  printUnixTime(timeBuf, startTime); DEBUG2_PRINTF("\nStartTime2 GMT:%s", timeBuf);

  E_RC rc = E_OK;

  for (unsigned int i = 0; i<ARCH_DAY_SIZE; i++) {
     invData.dayWh[i] = 0;
  }
  invData.hasDayData = false;

  int packetcount = 0;
  bool validPcktID = false;

  E_RC hasData = E_ARCHNODATA;
  pcktID++;
  writePacketHeader(pcktBuf, 0x01, invData.BTAddress);
  writePacket(pcktBuf, 0x09, 0xE0, 0, invData.SUSyID, invData.Serial);
  write32(pcktBuf, 0x70000200);
  write32(pcktBuf, startTime - 300);
  write32(pcktBuf, startTime + 86100);
  writePacketTrailer(pcktBuf);
  writePacketLength(pcktBuf);

  BTsendPacket(pcktBuf);

  do {
    totalWh = 0;
    totalWh_prev = 0;
    dateTime = 0;

    do {
      rc = getPacket(invData.BTAddress, 1);

      if (rc != E_OK) {
         DEBUG3_PRINTF("\ngetPacket error=%d", rc);
         return rc;
      }
      // packetcount=nr of packets left on multi packet transfer n..0
      packetcount = pcktBuf[25];
      DEBUG2_PRINTF("packetcount=%d\n", packetcount);

      //TODO: Move checksum validation to getPacket
      if (!validateChecksum())
        return E_CHKSUM;
      else {
        unsigned short rcvpcktID = get_u16(pcktBuf + 27) & 0x7FFF;
        if (validPcktID || (pcktID == rcvpcktID)) {
          validPcktID = true;
          for (int x = 41; x < (pcktBufPos - 3); x += 12) {
            dateTime = (time_t)get_u32(pcktBuf + x);
            uint16_t idx =((dateTime/3600)%24 * 12)+((dateTime/60)%60/5); //h*12+min/5

            totalWh = get_u64(pcktBuf + x + 4);
            if ((totalWh > 0) && (!invData.hasDayData)) { 
              invData.DayStartTime = dateTime;
              invData.hasDayData = true;
              hasData = E_OK; 
              printUnixTime(timeBuf, dateTime); 
              DEBUG1_PRINTF("\nArchiveDayData %s", timeBuf);
            }
            if (idx < ARCH_DAY_SIZE) {
              invData.dayWh[idx] = totalWh;
              value64 = (totalWh - totalWh_prev) * 60 / 5; // assume 5 min. interval
              DEBUG3_PRINTF("[%03u] %6llu Wh %6llu W\n", idx, totalWh, value64);
            }
            totalWh_prev = totalWh;
          } //for
        } else {
            DEBUG1_PRINTF("Packet ID mismatch. Exp. %d, rec. %d\n", pcktID, rcvpcktID);
            validPcktID = true;
            packetcount = 0;
        }
      }
    } while (packetcount > 0);
  } while (!validPcktID);

  /* print values
  time_t startT = invData.DayStartTime;
  printUnixTime(timeBuf, startT);
  DEBUG2_PRINTF("Day History: %s\n", timeBuf);
  totalWh_prev = 0;

  for (uint16_t i = 0; i<ARCH_DAY_SIZE; i++) {
    totalWh = invData.dayWh[i];
    value32=0;
    if ((totalWh>0) && (totalWh_prev>0)) {
      value32 = (uint32_t)((totalWh - totalWh_prev)*60/5); 
    }
    if (totalWh>0) {
      printUnixTime(timeBuf, startT+3600+i*60*5); // GMT+1 + 5 min. interval
      DEBUG2_PRINTF("[%03d] %11.3f kWh  %7.3f kW %s\n", i, tokWh(totalWh), tokW(value32), timeBuf);
    }
    totalWh_prev = totalWh;
  }
  
  return hasData;
}
*/
// ******* read SMA current data **********
E_RC ESP32_SMA_Inverter::ReadCurrentData() {
  if (!btConnected) {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "Bluetooth offline!\n");
    return E_NODATA;
  }
  if ((getInverterData(SpotACTotalPower)) != E_OK)  {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "SpotACTotalPower error!\n" ); // Pac
    return E_NODATA;
  }
  if ((getInverterData(SpotDCVoltage)) != E_OK)     {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "getSpotDCVoltage error!\n" ); // Udc + Idc
    return E_NODATA;
  }
  if ((getInverterData(SpotACVoltage)) != E_OK)     {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "getSpotACVoltage error!\n" ); // Uac + Iac
    return E_NODATA;
  }
  if ((getInverterData(EnergyProduction)) != E_OK)  {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "EnergyProduction error!\n" ); // E-Total + E-Today
    return E_NODATA;
  }
  if ((getInverterData(SpotGridFrequency)) != E_OK) {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "SpotGridFrequency error!\n");
    return E_NODATA;
  }
  if ((getInverterData(InverterTemp)) != E_OK) {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "InverterTemp error!\n");
    return E_NODATA;
  }
  if ((getInverterData(DeviceStatus)) != E_OK) {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "Device Status error!\n");
    return E_NODATA;
  }
  if ((getInverterData(GridRelayStatus)) != E_OK) {
    charLen += snprintf(charBuf+charLen, CHAR_BUF_MAX-charLen, "Grid Relay Status error!\n");
    return E_NODATA;
  }

//case 5: if ((getInverterData(SpotDCPower)) != E_OK)   DEBUG1_PRINTLN("getSpotDCPower error!"); //pcktBuf[23]=15 error!
//case 6: if ((getInverterData(SpotACPower)) != E_OK)   DEBUG1_PRINTLN("SpotACPower error!"   ); //pcktBuf[23]=15 error!
//case 7: if ((getInverterData(InverterTemp)) != E_OK)  DEBUG1_PRINTLN("InverterTemp error!"  ); //pcktBuf[23]=15 error!
//case 8: if ((getInverterData(OperationTime)) != E_OK) DEBUG1_PRINTLN("OperationTime error!" ); // OperTime + OperTime
  return E_OK;
} 



// **** receive BT byte *******
uint8_t ESP32_SMA_Inverter::BTgetByte() {
  readTimeout = false;
  //Returns a single byte from the bluetooth stream (with error timeout/reset)
  uint32_t time = 20000+millis(); // 20 sec 
  uint8_t  rec = 0;  

  while (!serialBT.available() ) {
    //delay(5);  //Wait for BT byte to arrive
    if (millis() > time) { 
      DEBUG2_PRINTLN("BTgetByte Timeout");
      readTimeout = true;
      break;
    }
  }

  if (!readTimeout) rec = serialBT.read();
  return rec;
}
// **** transmit BT buffer ****
void ESP32_SMA_Inverter::BTsendPacket( uint8_t *btbuffer ) {
  //DEBUG2_PRINTLN();
  for(int i=0;i<pcktBufPos;i++) {
    #ifdef DebugBT
    if (i==0) DEBUG2_PRINT("*** sStart=");
    if (i==1) DEBUG2_PRINT(" len=");
    if (i==3) {
      if ((0x7e ^ *(btbuffer+1) ^ *(btbuffer+2)) == *(btbuffer+3)) 
        DEBUG2_PRINT(" checkOK=");
      else  DEBUG2_PRINT(" checkFalse!!=");
    }
    if (i==4)  DEBUG2_PRINTF("\nfrMac[%d]=",i);
    if (i==10) DEBUG2_PRINTF(" toMac[%d]=",i);
    if (i==16) DEBUG2_PRINTF(" Type[%d]=",i);
    if ((i==18)||(i==18+16)||(i==18+32)||(i==18+48)) DEBUG2_PRINTF("\nsDat[%d]=",i);
    DEBUG2_PRINTF("%02X,", *(btbuffer+i)); // Print out what we are sending, in hex, for inspection.
    #endif
    serialBT.write( *(btbuffer+i) );  // Send to SMA via ESP32 bluetooth
  }
  HexDump(btbuffer, pcktBufPos, 10, 'T');
}


//-------------------------------------------------------------------------
// ********************************************************
void ESP32_SMA_Inverter::writeByte(uint8_t *btbuffer, uint8_t v) {
  //Keep a rolling checksum over the payload
  fcsChecksum = (fcsChecksum >> 8) ^ fcstab[(fcsChecksum ^ v) & 0xff];
  if (v == 0x7d || v == 0x7e || v == 0x11 || v == 0x12 || v == 0x13) {
    btbuffer[pcktBufPos++] = 0x7d;
    btbuffer[pcktBufPos++] = v ^ 0x20;
  } else {
    btbuffer[pcktBufPos++] = v;
  }
}
// ********************************************************
void ESP32_SMA_Inverter::write32(uint8_t *btbuffer, uint32_t v) {
  writeByte(btbuffer,(uint8_t)((v >> 0) & 0xFF));
  writeByte(btbuffer,(uint8_t)((v >> 8) & 0xFF));
  writeByte(btbuffer,(uint8_t)((v >> 16) & 0xFF));
  writeByte(btbuffer,(uint8_t)((v >> 24) & 0xFF));
}
// ********************************************************
void ESP32_SMA_Inverter::write16(uint8_t *btbuffer, uint16_t v) {
  writeByte(btbuffer,(uint8_t)((v >> 0) & 0xFF));
  writeByte(btbuffer,(uint8_t)((v >> 8) & 0xFF));
}
// ********************************************************
void ESP32_SMA_Inverter::writeArray(uint8_t *btbuffer, const uint8_t bytes[], int loopcount) {
    for (int i = 0; i < loopcount; i++) writeByte(btbuffer, bytes[i]);
}
// ********************************************************
//  writePacket(pcktBuf, 0x0E, 0xA0, 0x0100, 0xFFFF, 0xFFFFFFFF); // anySUSyID, anySerial);
void ESP32_SMA_Inverter::writePacket(uint8_t *buf, uint8_t longwords, uint8_t ctrl, uint16_t ctrl2, uint16_t dstSUSyID, uint32_t dstSerial) {
  buf[pcktBufPos++] = 0x7E;   //Not included in checksum
  write32(buf, BTH_L2SIGNATURE);
  writeByte(buf, longwords);
  writeByte(buf, ctrl);
  write16(buf, dstSUSyID);
  write32(buf, dstSerial);
  write16(buf, ctrl2);
  write16(buf, appSUSyID);
  write32(buf, appSerial);
  write16(buf, ctrl2);
  write16(buf, 0);
  write16(buf, 0);
  write16(buf, pcktID | 0x8000);
}
//-------------------------------------------------------------------------
void ESP32_SMA_Inverter::writePacketTrailer(uint8_t *btbuffer) {
  fcsChecksum ^= 0xFFFF;
  btbuffer[pcktBufPos++] = fcsChecksum & 0x00FF;
  btbuffer[pcktBufPos++] = (fcsChecksum >> 8) & 0x00FF;
  btbuffer[pcktBufPos++] = 0x7E;  //Trailing byte
}
//-------------------------------------------------------------------------
void ESP32_SMA_Inverter::writePacketLength(uint8_t *buf) {
  buf[1] = pcktBufPos & 0xFF;         //Lo-Byte
  buf[2] = (pcktBufPos >> 8) & 0xFF;  //Hi-Byte
  buf[3] = buf[0] ^ buf[1] ^ buf[2];      //checksum
}
//-------------------------------------------------------------------------
bool ESP32_SMA_Inverter::validateChecksum() {
  fcsChecksum = 0xffff;
  //Skip over 0x7e at start and end of packet
  for(int i = 1; i <= pcktBufPos - 4; i++) {
    fcsChecksum = (fcsChecksum >> 8) ^ fcstab[(fcsChecksum ^ pcktBuf[i]) & 0xff];
  }
  fcsChecksum ^= 0xffff;

  if (get_u16(pcktBuf+pcktBufPos-3) == fcsChecksum) {
    return true;
  } else {
    DEBUG1_PRINTF("Invalid validateChecksum 0x%04X not 0x%04X\n", 
    fcsChecksum, get_u16(pcktBuf+pcktBufPos-3));
    return false;
  }
}

