#include "printer_manager.h"

PrinterManager g_printer_manager;

PrinterManager::PrinterManager() {
    state = PrinterManagerState::DISCONNECTED;
    current_job_id = "";
    usb_connected_ = false;
}