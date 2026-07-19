#pragma once

// The DISP board's USB-CDC console (M2): the bench/RE + maintenance surface.
//   @INJ <id> <b0..>  inject a CAN RX frame (bench display handshakes)
//   @EMU <0|1>        bench self-ACK toggle
//   tx <id> <b0..>    transmit one raw CAN frame (RE primitive)
//   @PROG <hex>       USB<->link programming bridge: forwards one raw link
//                     frame ([type][payload] hex) toward GW; replies echo back
//                     as "@PROG <hex>" lines (tools/flash_gw.py speaks this)
namespace DispConsole
{
    void begin();
    void loop();
}
