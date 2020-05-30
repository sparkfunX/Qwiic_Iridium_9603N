// Qwiic Iridium 9603N interrupt routines

// I2C receiveEvent
// ================
// Handle a write to the IO pins or the Serial port
// If the first byte is IO_REG (0x10), read one more byte if present and write the contents to the IO pins.
// If the first byte is LEN_REG (0xFD), store it in last_address so we know what to do during the next requestEvent
// If the first byte is DATA_REG (0xFF), keep reading bytes (if any) and write them to the 9603 serial port.
void receiveEvent(int numberOfBytesReceived) {
  if (numberOfBytesReceived > 0) { // Check that we received some data (!) (hopefully redundant!)
    
    uint8_t incoming = Wire.read(); // Read the first byte
    last_address = incoming; // Store this byte in last_address so we know what to do during the next requestEvent

    if (incoming == IO_REG) { // Does the Master want to read or set the I/O pins?
      if (numberOfBytesReceived > 1) { // Did the Master send a second byte to set the I/O pins?
        uint8_t incoming2 = Wire.read(); // Read the second byte

        if (incoming2 & IO_SHDN) { // If the SHDN bit is ON
          digitalWrite(SHDN, SHDN__ON); // Turn the pin ON
        }
        else {
          digitalWrite(SHDN, SHDN__OFF); // Turn the pin OFF
        }
        
        if (incoming2 & IO_PWR_EN) { // If the PWR_EN bit is ON
          digitalWrite(PWR_EN, PWR_EN__ON); // Turn the pin ON
        }
        else {
          digitalWrite(PWR_EN, PWR_EN__OFF); // Turn the pin OFF
        }
        
        if (incoming2 & IO_ON_OFF) { // If the ON_OFF bit is ON
          digitalWrite(ON_OFF, ON_OFF__ON); // Turn the pin ON
        }
        else {
          digitalWrite(ON_OFF, ON_OFF__OFF); // Turn the pin OFF
        }
        
        if ((incoming2 & IO_RI) == 0x00) { // If the RI bit is clear
          RI_FLAG = false; // Clear the RI flag
        }
        
        LOW_POWER_MODE = ((incoming2 & IO_LOW_PWR) == IO_LOW_PWR); // Update low power mode
      }
      if (numberOfBytesReceived > 2) // Did we receive any unexpected extra data?
      {
        for (int i = 2; i < numberOfBytesReceived; i++) // If we did, mop up the extra bytes - hopefully redundant?!
        {
          Wire.read();
        }
      }
    }

    else if (incoming == DATA_REG) { // Does the Master want to write serial data to the 9603N?
      if (numberOfBytesReceived > 1) // If the Master sent any serial data, write it to the 9603 now
      {
        for (int i = 1; i < numberOfBytesReceived; i++)
        {
          uint8_t incoming2 = Wire.read();
          Serial.write(incoming2);
        }
      }
    }

    else if (incoming == LEN_REG) { // Does the Master want to read the available serial length?
      // We shouldn't need to do anything (except update last_address)
      if (numberOfBytesReceived > 1) // Did we receive any unexpected extra data?
      {
        for (int i = 1; i < numberOfBytesReceived; i++) // If we did, mop up the extra bytes - hopefully redundant?!
        {
          Wire.read();
        }
      }
    }
  }
  
  last_activity = millis(); // Update last_activity
}

// I2C requestEvent
// ================
// Handle a read of the IO pins or the Serial port
// If last_address was IO_REG (0x10) then return the status of the IO pins
// If last_address was LEN_REG (0xFD), return the MSB and LSB of the serial buffer length and store them in serAvailMSB/LSB
// If last_address was DATA_REG (0xFF), return up to SER_PACKET_SIZE bytes from the serial buffer and update serAvailMSB/LSB
void requestEvent()
{
  if (last_address == IO_REG) // Return the status of the IO pins
  {
    // Read the IO pins one at a time and set or clear bits in IO_REGISTER appropriately
    
    if (digitalRead(SHDN) == SHDN__ON) { // If the SHDN pin is in its ON state (not necessarily that it is HIGH)
      IO_REGISTER |= IO_SHDN; // Set the SHDN bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_SHDN; // Clear the SHDN bit in IO_REGISTER
    }
    
    if (digitalRead(PWR_EN) == PWR_EN__ON) { // If the PWR_EN pin is in its ON state (not necessarily that it is HIGH)
      IO_REGISTER |= IO_PWR_EN; // Set the PWR_EN bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_PWR_EN; // Clear the PWR_EN bit in IO_REGISTER
    }
    
    if (digitalRead(ON_OFF) == ON_OFF__ON) { // If the ON_OFF pin is in its ON state (not necessarily that it is HIGH)
      IO_REGISTER |= IO_ON_OFF; // Set the ON_OFF bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_ON_OFF; // Clear the ON_OFF bit in IO_REGISTER
    }
    
    if (RI_FLAG == true) { // If the RI_FLAG is true (set by the ISR)
      IO_REGISTER |= IO_RI; // Set the RI bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_RI; // Clear the RI bit in IO_REGISTER
    }
    
    if (digitalRead(NA) == NA__ON) { // If the NA pin is in its ON state (not necessarily that it is HIGH)
      IO_REGISTER |= IO_NA; // Set the NA bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_NA; // Clear the NA bit in IO_REGISTER
    }
    
    if (digitalRead(PGOOD) == PGOOD__ON) { // If the PGOOD pin is in its ON state (not necessarily that it is HIGH)
      IO_REGISTER |= IO_PGOOD; // Set the PGOOD bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_PGOOD; // Clear the PGOOD bit in IO_REGISTER
    }
  
    if (LOW_POWER_MODE) { // If low power mode is enabled
      IO_REGISTER |= IO_LOW_PWR; // Set the low power bit in IO_REGISTER
    }
    else {
      IO_REGISTER &= ~IO_LOW_PWR; // Clear the low power bit in IO_REGISTER
    }
  
    // Now write IO_REGISTER to I2C
    Wire.write(IO_REGISTER);

    // Reset last_address to zero
    last_address = 0;
  }

  else if (last_address == LEN_REG) // Return the MSB and LSB of the serial buffer length and store them in serAvailMSB/LSB
  {
    //Check how many bytes are available in the serial buffer
    uint16_t avail = Serial.available();
    
    //Store the MSB for the subsequent requestEvent
    serAvailMSB = (uint8_t)((avail & 0xFF00) >> 8);
    
    //Store the LSB for the subsequent  requestEvent
    serAvailLSB = (uint8_t)(avail & 0xFF);

    uint8_t buff[2];
    buff[0] = serAvailMSB;
    buff[1] = serAvailLSB;

    Wire.write(buff, 2); // Return the available bytes in MSB, LSB format

    // Reset last_address to zero
    last_address = 0;
  }

  else if (last_address == DATA_REG) // Return up to SER_PACKET_SIZE bytes from the serial buffer and update serAvailMSB/LSB
  {
    // Check how many bytes the Master is expecting us to send
    uint16_t avail = (((uint16_t)serAvailMSB) << 8) | (uint16_t)serAvailLSB;
    uint8_t buff[SER_PACKET_SIZE];
    if (avail > SER_PACKET_SIZE) // If there are more than SER_PACKET_SIZE bytes to write
    {
      avail = avail - SER_PACKET_SIZE; // Decrease the available bytes by SER_PACKET_SIZE
      // Update the number of available bytes and leave last_address set to DATA_REG
      serAvailMSB = (uint8_t)((avail & 0xFF00) >> 8);
      serAvailLSB = (uint8_t)(avail & 0xFF);
      for (int i = 0; i < SER_PACKET_SIZE; i++) // For each of the bytes
      {
        buff[i] = Serial.read(); // Read a byte from the serial buffer
      }
      Wire.write(buff, SER_PACKET_SIZE); // Write the bytes
    }
    else if (avail > 0) // If there are <= SER_PACKET_SIZE bytes left (but more than zero)
    {
      serAvailMSB = 0; // Zero the number of available bytes (redundant?)
      serAvailLSB = 0;
      for (int i = 0; i < avail; i++) // For each byte
      {
        buff[i] = Serial.read(); // Read a byte from the serial buffer
      }
      Wire.write(buff, avail); // Write the bytes
      last_address = 0; // Reset last_address to zero now all bytes have been written
    }
    else // If there are zero bytes to send - this should hopefully be redundant/impossible?
    {
      last_address = 0; // Reset last_address to zero
    }
  }

  last_activity = millis(); // Update last_activity
}
