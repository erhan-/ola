/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * SpiDmxThread.cpp
 * This thread runs while one or more ports are registered. It simultaneously
 * reads / writes SPI data and then calls the parser. This is repeated forever.
 * Copyright (C) 2017 Florian Edelmann
 */

#include <string>
#include "ola/Logging.h"
#include "plugins/spidmx/SpiDmxWidget.h"
#include "plugins/spidmx/SpiDmxThread.h"
#include "plugins/spidmx/SpiDmxParser.h"

namespace ola {
namespace plugin {
namespace spidmx {

SpiDmxThread::SpiDmxThread(SpiDmxWidget *widget, unsigned int blocklength)
  : m_widget(widget),
    m_blocklength(blocklength),
    m_term(false),
    m_registered_ports(0),
    m_spi_rx_buffer(blocklength),
    m_spi_tx_buffer(blocklength) {
}

SpiDmxThread::~SpiDmxThread() {
  Stop();
}

/**
 * This thread does only have to run if ports using it are patched to a
 * universe. Thus, they must register and unregister to notify this thread.
 */
void SpiDmxThread::RegisterPort() {
  m_registered_ports++;

  if (m_registered_ports >= 1) {
    Start();
  }
}
void SpiDmxThread::UnregisterPort() {
  m_registered_ports--;

  if (m_registered_ports <= 0) {
    Stop();
  }
}


/**
 * Stop this thread
 */
bool SpiDmxThread::Stop() {
  {
    ola::thread::MutexLocker locker(&m_term_mutex);
    m_term = true;
  }
  return Join();
}


/**
 * Copy a DmxBuffer to the output thread
 */
bool SpiDmxThread::WriteDMX(const DmxBuffer &buffer) {
  ola::thread::MutexLocker locker(&m_buffer_mutex);
  m_dmx_tx_buffer.Set(buffer);
  return true;
}

/**
  * @brief Get DMX Buffer
  * @returns DmxBuffer with current input values.
  */
const DmxBuffer &SpiDmxThread::GetDmxInBuffer() const {
  return m_dmx_rx_buffer;
}


/**
  * @brief Set the callback to be called when the receive buffer is updated.
  * @param callback The callback to call.
  */
bool SpiDmxThread::SetReceiveCallback(Callback0<void> *callback) {
  m_receive_callback.reset(callback);

  if (!callback) {
    // InputPort unregistered
    UnregisterPort();
    return true;
  }

  if (m_widget->SetupOutput()) {
    RegisterPort();
    return true;
  }

  return false;
}


/**
 * The method called by the thread
 */
void *SpiDmxThread::Run() {
  DmxBuffer dmx_buffer;

  uint8_t *spi_rx_ptr;
  uint8_t *spi_tx_ptr;

  // Setup the widget
  if (!m_widget->IsOpen()) {
    if (!m_widget->SetupOutput()) {
      OLA_INFO << "SpiDmxThread stopped because SPI widget could not be opened";
      return NULL;
    }
  }

  // Setup the parser
  SpiDmxParser *parser = new SpiDmxParser(&m_dmx_rx_buffer,
                                          m_receive_callback.get());

  while (1) {
    {
      ola::thread::MutexLocker locker(&m_term_mutex);
      if (m_term) {
        break;
      }
    }

    {
      ola::thread::MutexLocker locker(&m_buffer_mutex);
      dmx_buffer.Set(m_dmx_tx_buffer);
    }

    // TODO(FloEdelmann) fill m_spi_tx_buffer with the values from dmx_buffer
    //                   (each bit repeated 8 times)

    // vectors store their contents contiguously,
    // so we can get a pointer to the elements like so
    spi_rx_ptr = &m_spi_rx_buffer[0];
    spi_tx_ptr = &m_spi_tx_buffer[0];

    if (!m_widget->ReadWrite(spi_tx_ptr, spi_rx_ptr, m_blocklength)) {
      OLA_WARN << "SPIDMX Read / Write failed, stopping thread...";
      break;
    }

    parser->ParseDmx(spi_rx_ptr, (uint64_t) m_blocklength);
  }

  delete parser;

  return NULL;
}

}  // namespace spidmx
}  // namespace plugin
}  // namespace ola
