#include "usb_dirtyjtag.hpp"
#include "dirtyjtag_descriptors.hpp"
#include "usb_serial.hpp"
#include "packet_order.hpp"
#include "time.hpp"

extern "C" {
#include <string.h>
}

#define UEP_IN  0x80
#define UEP_OUT 0x00
#define UEP0    0x00
#define UEP1    0x01
#define UEP2    0x02
#define UEP3    0x03
#define UEP4    0x04

#define UDM_PUE_MASK 0x00000003

namespace Desc = DirtyJtagUsbDescriptors;

namespace
{
constexpr uint32_t kAbandonedReplyTimeoutMs = 1000;

struct __attribute__((packed)) UsbSetupRequest
{
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

constexpr uint8_t USB_GET_STATUS = 0x00;
constexpr uint8_t USB_CLEAR_FEATURE = 0x01;
constexpr uint8_t USB_SET_ADDRESS = 0x05;
constexpr uint8_t USB_GET_DESCRIPTOR = 0x06;
constexpr uint8_t USB_GET_CONFIGURATION = 0x08;
constexpr uint8_t USB_SET_CONFIGURATION = 0x09;
constexpr uint8_t USB_GET_INTERFACE = 0x0a;
constexpr uint8_t USB_SET_INTERFACE = 0x0b;
constexpr uint8_t USB_REQ_TYP_MASK = 0x60;
constexpr uint8_t USB_REQ_TYP_STANDARD = 0x00;
constexpr uint8_t USB_REQ_RECIP_MASK = 0x1f;
constexpr uint8_t USB_REQ_RECIP_ENDP = 0x02;
constexpr uint8_t USB_REQ_FEAT_ENDP_HALT = 0x00;
constexpr uint8_t USB_DESCR_TYP_DEVICE = 0x01;
constexpr uint8_t USB_DESCR_TYP_CONFIG = 0x02;
constexpr uint8_t USB_DESCR_TYP_STRING = 0x03;

constexpr uint8_t USBFS_UEP1_RX_EN = RB_UEP1_RX_EN;
constexpr uint8_t USBFS_UEP4_RX_EN = RB_UEP4_RX_EN;
constexpr uint8_t USBFS_UEP2_TX_EN = RB_UEP2_TX_EN;
constexpr uint8_t USBFS_UEP3_TX_EN = RB_UEP3_TX_EN;
constexpr uint8_t USBFS_UD_PD_DIS = RB_UD_PD_DIS;
constexpr uint8_t USBFS_UD_PORT_EN = RB_UD_PORT_EN;
constexpr uint8_t USBFS_UIF_TRANSFER = RB_UIF_TRANSFER;
constexpr uint8_t USBFS_UIF_BUS_RST = RB_UIF_BUS_RST;
constexpr uint8_t USBFS_UIF_SUSPEND = RB_UIF_SUSPEND;
constexpr uint8_t USBFS_UIS_TOKEN_MASK = MASK_UIS_TOKEN;
constexpr uint8_t USBFS_UIS_TOKEN_IN = UIS_TOKEN_IN;
constexpr uint8_t USBFS_UIS_TOKEN_OUT = UIS_TOKEN_OUT;
constexpr uint8_t USBFS_UIS_TOKEN_SETUP = UIS_TOKEN_SETUP;
constexpr uint8_t USBFS_UIS_ENDP_MASK = MASK_UIS_ENDP;
constexpr uint8_t USBFS_UIS_TOG_OK = RB_UIS_TOG_OK;
}

UsbDirtyJtag* UsbDirtyJtag::self_ = nullptr;

void UsbDirtyJtag::endpointInit()
{
    USBFSD->UEP4_1_MOD = USBFS_UEP1_RX_EN | USBFS_UEP4_RX_EN;
    USBFSD->UEP2_3_MOD = USBFS_UEP2_TX_EN | USBFS_UEP3_TX_EN;

    USBFSD->UEP0_DMA = (uint32_t)ep0_;
    USBFSD->UEP1_DMA = (uint32_t)ep1Out_;
    USBFSD->UEP2_DMA = (uint32_t)ep2In_;
    USBFSD->UEP3_DMA = (uint32_t)ep3In_;

    USBFSD->UEP0_CTRL_H = USBFS_UEP_R_RES_ACK | USBFS_UEP_T_RES_NAK;
    resetDirtyJtagEndpoints(true, true);
    resetCmsisDapEndpoints(true, true);
    arrivalCounter_ = 0;
    dirtyJtagArrival_ = 0;
    cmsisDapArrival_ = 0;
}

void UsbDirtyJtag::resetDirtyJtagEndpoints(bool resetOutToggle, bool resetInToggle)
{
    uint16_t outToggle = USBFSD->UEP1_CTRL_H & USBFS_UEP_R_TOG;
    uint16_t inToggle = USBFSD->UEP2_CTRL_H & USBFS_UEP_T_TOG;
    if (resetOutToggle) outToggle = 0;
    if (resetInToggle) inToggle = 0;

    USBFSD->UEP2_TX_LEN = 0;
    USBFSD->UEP1_CTRL_H = outToggle | USBFS_UEP_R_RES_ACK;
    USBFSD->UEP2_CTRL_H = inToggle | USBFS_UEP_T_RES_NAK;
    pendingLength_ = 0;
    packetPending_ = false;
    packetTaken_ = false;
    txBusy_ = false;
    txStartedMs_ = 0;
}

void UsbDirtyJtag::resetCmsisDapEndpoints(bool resetOutToggle, bool resetInToggle)
{
    uint16_t outToggle = USBFSD->UEP4_CTRL_H & USBFS_UEP_R_TOG;
    uint16_t inToggle = USBFSD->UEP3_CTRL_H & USBFS_UEP_T_TOG;
    if (resetOutToggle) outToggle = 0;
    if (resetInToggle) inToggle = 0;

    USBFSD->UEP3_TX_LEN = 0;
    USBFSD->UEP4_CTRL_H = outToggle | USBFS_UEP_R_RES_ACK;
    USBFSD->UEP3_CTRL_H = inToggle | USBFS_UEP_T_RES_NAK;
    dapPendingLength_ = 0;
    dapPacketPending_ = false;
    dapPacketTaken_ = false;
    dapTxBusy_ = false;
    dapTxStartedMs_ = 0;
}

void UsbDirtyJtag::init()
{
    self_ = this;
    dirtyJtagResetPending_ = false;
    cmsisDapResetPending_ = false;

    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC;
    RCC->AHBPCENR |= RCC_AHBPeriph_USBFS;

    GPIOC->BSXR = GPIO_Pin_17 >> 16u;
    GPIOC->CFGXR = (GPIOC->CFGXR & ~0xffu) |
                   (GPIO_CFGLR_IN_FLOAT << 0) |
                   (GPIO_CFGLR_IN_PUPD << 4);

    AFIO->CTLR = (AFIO->CTLR & ~(UDP_PUE_MASK | UDM_PUE_MASK)) |
                 USB_PHY_V33 | UDP_PUE_1K5 | USB_IOEN;

    USBFSD->BASE_CTRL = 0;
    endpointInit();
    USBFSD->DEV_ADDR = 0;
    USBFSD->BASE_CTRL = USBFS_UC_DEV_PU_EN | USBFS_UC_INT_BUSY | USBFS_UC_DMA_EN;
    USBFSD->INT_FG = 0xFF;
    USBFSD->UDEV_CTRL = USBFS_UD_PD_DIS | USBFS_UD_PORT_EN;
    USBFSD->INT_EN = USBFS_UIE_SUSPEND | USBFS_UIE_BUS_RST | USBFS_UIE_TRANSFER;

    NVIC_EnableIRQ(USBFS_IRQn);
}

void UsbDirtyJtag::armOut()
{
    USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                          USBFS_UEP_R_RES_ACK;
}

void UsbDirtyJtag::armCmsisDapOut()
{
    USBFSD->UEP4_CTRL_H = (USBFSD->UEP4_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                          USBFS_UEP_R_RES_ACK;
}

bool UsbDirtyJtag::takeNextPacket(uint8_t* destination, size_t& length, bool& cmsisDap)
{
    bool available = false;
    NVIC_DisableIRQ(USBFS_IRQn);
    const bool dirtyAvailable = packetPending_ && !packetTaken_;
    const bool dapAvailable = dapPacketPending_ && !dapPacketTaken_;
    const bool dapFirst = dapAvailable &&
        (!dirtyAvailable || packetArrivedBefore(cmsisDapArrival_, dirtyJtagArrival_));

    if (dapFirst)
    {
        length = dapPendingLength_;
        memcpy(destination, ep0_ + kPacketSize, length);
        dapPacketTaken_ = true;
        cmsisDap = true;
        available = true;
    }
    else if (dirtyAvailable)
    {
        length = pendingLength_;
        memcpy(destination, ep1Out_, length);
        packetTaken_ = true;
        cmsisDap = false;
        available = true;
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    return available;
}

bool UsbDirtyJtag::takeSessionReset(bool& dirtyJtag, bool& cmsisDap)
{
    NVIC_DisableIRQ(USBFS_IRQn);
    const uint32_t now = Time::millis();
    dirtyJtag = dirtyJtagResetPending_ ||
        (txBusy_ &&
         (uint32_t)(now - txStartedMs_) >= kAbandonedReplyTimeoutMs);
    cmsisDap = cmsisDapResetPending_ ||
        (dapTxBusy_ &&
         (uint32_t)(now - dapTxStartedMs_) >= kAbandonedReplyTimeoutMs);
    dirtyJtagResetPending_ = false;
    cmsisDapResetPending_ = false;
    if (dirtyJtag) resetDirtyJtagEndpoints(false, false);
    if (cmsisDap) resetCmsisDapEndpoints(false, false);
    NVIC_EnableIRQ(USBFS_IRQn);
    return dirtyJtag || cmsisDap;
}

bool UsbDirtyJtag::finish(const uint8_t* response, size_t length)
{
    if (length > kPacketSize) return false;

    bool accepted = false;
    NVIC_DisableIRQ(USBFS_IRQn);
    if (packetPending_ && packetTaken_ && !txBusy_)
    {
        packetPending_ = false;
        packetTaken_ = false;
        pendingLength_ = 0;

        if (length != 0)
        {
            memcpy(ep2In_, response, length);
            USBFSD->UEP2_TX_LEN = (uint16_t)length;
            USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                  USBFS_UEP_T_RES_ACK;
            txBusy_ = true;
            txStartedMs_ = Time::millis();
        }
        else
        {
            armOut();
        }
        accepted = true;
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    return accepted;
}

bool UsbDirtyJtag::finishCmsisDap(const uint8_t* response, size_t length)
{
    if (length > kPacketSize) return false;

    bool accepted = false;
    NVIC_DisableIRQ(USBFS_IRQn);
    if (dapPacketPending_ && dapPacketTaken_ && !dapTxBusy_)
    {
        dapPacketPending_ = false;
        dapPacketTaken_ = false;
        dapPendingLength_ = 0;

        if (length != 0)
        {
            memcpy(ep3In_, response, length);
            USBFSD->UEP3_TX_LEN = (uint16_t)length;
            USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                  USBFS_UEP_T_RES_ACK;
            dapTxBusy_ = true;
            dapTxStartedMs_ = Time::millis();
        }
        else
        {
            armCmsisDapOut();
        }
        accepted = true;
    }
    NVIC_EnableIRQ(USBFS_IRQn);
    return accepted;
}

void UsbDirtyJtag::handleSetup()
{
    const auto* request = reinterpret_cast<const UsbSetupRequest*>(ep0_);
    uint16_t length = 0;
    bool error = false;

    USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_NAK |
                          USBFS_UEP_R_TOG | USBFS_UEP_R_RES_NAK;

    descriptorCursor_ = nullptr;
    setupRequestType_ = request->bRequestType;
    setupRequest_ = request->bRequest;
    setupLength_ = request->wLength;
    setupValue_ = request->wValue;
    setupIndex_ = request->wIndex;

    if ((setupRequestType_ & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
    {
        error = true;
    }
    else
    {
        switch (setupRequest_)
        {
            case USB_GET_DESCRIPTOR:
                switch ((uint8_t)(setupValue_ >> 8))
                {
                    case USB_DESCR_TYP_DEVICE:
                        descriptorCursor_ = Desc::device;
                        length = Desc::device[0];
                        break;
                    case USB_DESCR_TYP_CONFIG:
                        descriptorCursor_ = Desc::configuration;
                        length = Desc::configurationLength();
                        break;
                    case USB_DESCR_TYP_STRING:
                        switch ((uint8_t)setupValue_)
                        {
                            case Desc::Lang: descriptorCursor_ = Desc::lang; length = Desc::lang[0]; break;
                            case Desc::Manufacturer: descriptorCursor_ = Desc::manufacturer; length = Desc::manufacturer[0]; break;
                            case Desc::Product: descriptorCursor_ = Desc::product; length = Desc::product[0]; break;
                            case Desc::Serial: descriptorCursor_ = UsbSerial::serialDescriptor(); length = descriptorCursor_[0]; break;
                            case Desc::CmsisDapInterface: descriptorCursor_ = Desc::cmsisDapInterface; length = Desc::cmsisDapInterface[0]; break;
                            default: error = true; break;
                        }
                        break;
                    default:
                        error = true;
                        break;
                }
                if (!error)
                {
                    if (setupLength_ > length) setupLength_ = length;
                    length = setupLength_ > Desc::kEp0Size ? Desc::kEp0Size : setupLength_;
                    memcpy(ep0_, descriptorCursor_, length);
                    descriptorCursor_ += length;
                }
                break;

            case USB_SET_ADDRESS:
                deviceAddress_ = (uint8_t)setupValue_;
                break;

            case USB_GET_CONFIGURATION:
                ep0_[0] = deviceConfiguration_;
                if (setupLength_ > 1) setupLength_ = 1;
                break;

            case USB_SET_CONFIGURATION:
                if (setupValue_ > 1)
                {
                    error = true;
                    break;
                }
                deviceConfiguration_ = (uint8_t)setupValue_;
                configured_ = deviceConfiguration_ != 0;
                resetDirtyJtagEndpoints(true, true);
                resetCmsisDapEndpoints(true, true);
                dirtyJtagResetPending_ = true;
                cmsisDapResetPending_ = true;
                break;

            case USB_GET_INTERFACE:
                ep0_[0] = 0;
                if (setupLength_ > 1) setupLength_ = 1;
                break;

            case USB_SET_INTERFACE:
                if (setupValue_ != 0 || !configured_)
                {
                    error = true;
                    break;
                }
                if (setupIndex_ == 0)
                {
                    resetDirtyJtagEndpoints(true, true);
                    dirtyJtagResetPending_ = true;
                }
                else if (setupIndex_ == 1)
                {
                    resetCmsisDapEndpoints(true, true);
                    cmsisDapResetPending_ = true;
                }
                else error = true;
                break;

            case USB_GET_STATUS:
                ep0_[0] = 0;
                ep0_[1] = 0;
                if (setupLength_ > 2) setupLength_ = 2;
                break;

            case USB_CLEAR_FEATURE:
                if ((setupRequestType_ & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP &&
                    setupValue_ == USB_REQ_FEAT_ENDP_HALT)
                {
                    switch (setupIndex_)
                    {
                        case (UEP_OUT | UEP1):
                            resetDirtyJtagEndpoints(true, false);
                            dirtyJtagResetPending_ = true;
                            break;
                        case (UEP_IN | UEP2):
                            resetDirtyJtagEndpoints(false, true);
                            dirtyJtagResetPending_ = true;
                            break;
                        case (UEP_OUT | UEP4):
                            resetCmsisDapEndpoints(true, false);
                            cmsisDapResetPending_ = true;
                            break;
                        case (UEP_IN | UEP3):
                            resetCmsisDapEndpoints(false, true);
                            cmsisDapResetPending_ = true;
                            break;
                        default: error = true; break;
                    }
                }
                else
                {
                    error = true;
                }
                break;

            default:
                error = true;
                break;
        }
    }

    if (error)
    {
        USBFSD->UEP0_CTRL_H = USBFS_UEP_T_TOG | USBFS_UEP_T_RES_STALL |
                              USBFS_UEP_R_TOG | USBFS_UEP_R_RES_STALL;
    }
    else if (setupRequestType_ & UEP_IN)
    {
        length = setupLength_ > Desc::kEp0Size ? Desc::kEp0Size : setupLength_;
        setupLength_ -= length;
        USBFSD->UEP0_TX_LEN = length;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                              USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
    else
    {
        USBFSD->UEP0_TX_LEN = 0;
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                              USBFS_UEP_T_TOG | USBFS_UEP_T_RES_ACK;
    }
}

void UsbDirtyJtag::handleEp0In()
{
    if (setupLength_ == 0)
    {
        USBFSD->UEP0_CTRL_H = (USBFSD->UEP0_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                              USBFS_UEP_R_TOG | USBFS_UEP_R_RES_ACK;
    }

    if (setupRequest_ == USB_GET_DESCRIPTOR && setupLength_ != 0)
    {
        const uint16_t length = setupLength_ > Desc::kEp0Size ? Desc::kEp0Size : setupLength_;
        memcpy(ep0_, descriptorCursor_, length);
        descriptorCursor_ += length;
        setupLength_ -= length;
        USBFSD->UEP0_TX_LEN = length;
        USBFSD->UEP0_CTRL_H ^= USBFS_UEP_T_TOG;
    }
    else if (setupRequest_ == USB_SET_ADDRESS)
    {
        USBFSD->DEV_ADDR = (USBFSD->DEV_ADDR & USBFS_UDA_GP_BIT) | deviceAddress_;
    }
}

void UsbDirtyJtag::busReset()
{
    configured_ = false;
    deviceConfiguration_ = 0;
    deviceAddress_ = 0;
    sleepStatus_ = 0;
    USBFSD->DEV_ADDR = 0;
    endpointInit();
    dirtyJtagResetPending_ = true;
    cmsisDapResetPending_ = true;
}

void UsbDirtyJtag::onIrq()
{
    const uint8_t flags = USBFSD->INT_FG;
    const uint8_t status = USBFSD->INT_ST;

    if (flags & USBFS_UIF_TRANSFER)
    {
        switch (status & USBFS_UIS_TOKEN_MASK)
        {
            case USBFS_UIS_TOKEN_IN:
                if ((status & USBFS_UIS_ENDP_MASK) == UEP0)
                {
                    handleEp0In();
                }
                else if ((status & USBFS_UIS_ENDP_MASK) == UEP2)
                {
                    USBFSD->UEP2_CTRL_H ^= USBFS_UEP_T_TOG;
                    USBFSD->UEP2_CTRL_H = (USBFSD->UEP2_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                          USBFS_UEP_T_RES_NAK;
                    txBusy_ = false;
                    txStartedMs_ = 0;
                    armOut();
                }
                else if ((status & USBFS_UIS_ENDP_MASK) == UEP3)
                {
                    USBFSD->UEP3_CTRL_H ^= USBFS_UEP_T_TOG;
                    USBFSD->UEP3_CTRL_H = (USBFSD->UEP3_CTRL_H & ~USBFS_UEP_T_RES_MASK) |
                                          USBFS_UEP_T_RES_NAK;
                    dapTxBusy_ = false;
                    dapTxStartedMs_ = 0;
                    armCmsisDapOut();
                }
                break;

            case USBFS_UIS_TOKEN_OUT:
                if ((status & USBFS_UIS_ENDP_MASK) == UEP1 &&
                    (status & USBFS_UIS_TOG_OK))
                {
                    USBFSD->UEP1_CTRL_H ^= USBFS_UEP_R_TOG;
                    USBFSD->UEP1_CTRL_H = (USBFSD->UEP1_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                                          USBFS_UEP_R_RES_NAK;
                    pendingLength_ = (uint8_t)USBFSD->RX_LEN;
                    dirtyJtagArrival_ = ++arrivalCounter_;
                    packetPending_ = true;
                    packetTaken_ = false;
                }
                else if ((status & USBFS_UIS_ENDP_MASK) == UEP4 &&
                         (status & USBFS_UIS_TOG_OK))
                {
                    USBFSD->UEP4_CTRL_H ^= USBFS_UEP_R_TOG;
                    USBFSD->UEP4_CTRL_H = (USBFSD->UEP4_CTRL_H & ~USBFS_UEP_R_RES_MASK) |
                                          USBFS_UEP_R_RES_NAK;
                    dapPendingLength_ = (uint8_t)USBFSD->RX_LEN;
                    cmsisDapArrival_ = ++arrivalCounter_;
                    dapPacketPending_ = true;
                    dapPacketTaken_ = false;
                }
                break;

            case USBFS_UIS_TOKEN_SETUP:
                handleSetup();
                break;

            default:
                break;
        }
        USBFSD->INT_FG = USBFS_UIF_TRANSFER;
    }
    else if (flags & USBFS_UIF_BUS_RST)
    {
        busReset();
        USBFSD->INT_FG = USBFS_UIF_BUS_RST;
    }
    else if (flags & USBFS_UIF_SUSPEND)
    {
        USBFSD->INT_FG = USBFS_UIF_SUSPEND;
        if (USBFSD->MIS_ST & USBFS_UMS_SUSPEND)
        {
            sleepStatus_ |= 0x02;
            dirtyJtagResetPending_ = true;
            cmsisDapResetPending_ = true;
        }
        else                                    sleepStatus_ &= (uint8_t)~0x02;
    }
    else
    {
        USBFSD->INT_FG = flags;
    }
}

extern "C" void USBFS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
extern "C" void USBFS_IRQHandler(void)
{
    UsbDirtyJtag::handleIrq();
}
