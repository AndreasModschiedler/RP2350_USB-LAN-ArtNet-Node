# RP2350_USB-LAN-ArtNet-Node

I need to do a very robust and timing critical embedded development for an ArtNet Node with virtual Network Connection via USB.
* very similar and with technologies used in https://github.com/AdamHallGroup/P002407_CAMEO_NODE_ONE
* but built from scratch according to requirements specified in this README.md file

# PINS
Pins shall be used exactly as defined in https://github.com/AdamHallGroup/P002407_CAMEO_NODE_ONE/blob/main/firmware/src/pins.h
Can be copied to this project.

# Most important design goals and quality that must be met by this development
* the device shall be monitored with watchdog timer and needs to be reset if it breaks down for being able to recover its availability and processing
* the device will have stress by arriving network traffic and shall not break down by this
* the development shall implement a robust implementation of network interface via USB
* the device shall have a DHCP server that acts on a default network address 10.0.0.1 and asigns the next free address 10.0.0.2 to the PC right after connection and the PC asking for DHCP server to assign an address to it
* the device shall listen to incoming ethernet messages - but shall only process ArtNet messages on the corresponding port. All other traffic shall be ignored and buffers need to be freed for stable operation
* the device acts as gateway to an RS485 based DMX/RDM bus that shall comply to ESTA Standards of E1.20
* the device shall have 2 working modes:
  * mode RDM that sends out a dmx frame at minimum frequency defined for DMX-512 by ESTA
  * mode DMX that handles ArtCommand and ArtDMX only and sends out dmx frames at about 40HZ

# Requirements for switching modes
* the device shall process ArtCommand as follows:
  * Data content "MODE=DMX" shall switch the device operation mode to mode DMX
  * Data content "MODE=RDM" shall switch the device operation mode to mode RDM
  * Data content "FirmwareUpdate" shall switch the device into mass storrage mode for firmware updates (just like in the CAMEO_NODE_ONE project)

# Implementation of buffers and handling of incomming network traffic when buffers are full
* for sending out DMX over the bus the implementation shall have a DmxFrameBuffer that handles a full frame of DMX values for maximum number of channels
* due to the RS485 bus is slower than the network connection, the device shall handle incoming ArtRDM messages with buffers and priorities
  * the device shall put data load of incoming ArtRdm Request data into a buffer RdmRequestBuffer that can handle up to 5 buffered requests
  * requests in the RdmRequestBuffer shall be processed in First-in-First-out principle
  * if a buffered Request is processed and the request has been sent to the responder, the implementation shall wait for response from the responder
  * if the response is invalid or missing for 100ms, the request shall be sent again for two other tries
  * after getting valid response or final failing to get a valid response, the response or information on fail shall be sent over network to the ArtRdm requestor
  * after sending response to ArtRdm requestor the buffer slot shall be freed but sequence need to be intact and arriving ArtRdm requests need to be sheduled in the corresponding slot in row
  * if the ArtRdmBuffer is full, incoming ArtRDM requests shall be refused

# ArtPoll and ArtPollReply
Incoming ArtPoll messages shall be replied with corresponding ArtPollReply - just like in CAMEO_NODE_ONE project.
Manufacturer and Product data just like in this project.

# Discovery 
* the device shall handle Discovery Process compliant to E1.20
* background discovery every 10 seconds but the PC software shall only get a new Table of Device data if the devices on the DMX/RDM Bus have changed

# TOD Control with flush shall empty cached TOD data
* if my PC software requests a Table of Device Controll with flush, the device shall empty its cached list of devices found on the DMX/RDM bus

  
