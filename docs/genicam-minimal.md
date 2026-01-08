# Minimal GenICam XML for GVCP Proxy

This note describes the smallest GenICam XML needed for Aravis to discover a device, configure stream parameters, and start acquisition when using the host-side GVCP proxy.

The simplest path is to start from `src/arv-fake-camera.xml` and remove anything you do not need.

## Required feature groups

The following feature categories are the minimum set for typical Aravis clients:

- `DeviceControl` (identification strings)
- `ImageFormatControl` (width/height/pixel format)
- `AcquisitionControl` (start/stop commands)
- `TransportLayerControl` (payload size)

## Required features and registers

Use these addresses if you want to stay compatible with Aravis' fake camera defaults:

- `DeviceVendorName` (StringReg) at `0x48`, length 32, RO
- `DeviceModelName` (StringReg) at `0x68`, length 32, RO
- `DeviceVersion` (StringReg) at `0x88`, length 32, RO
- `DeviceID` / serial (StringReg) at `0xD8`, length 16, RO
- `SensorWidth` (Integer -> IntReg) at `0x11c`, RO
- `SensorHeight` (Integer -> IntReg) at `0x118`, RO
- `Width` (Integer -> IntReg) at `0x100`, RW
- `Height` (Integer -> IntReg) at `0x104`, RW
- `OffsetX` (Integer -> IntReg) at `0x130`, RW
- `OffsetY` (Integer -> IntReg) at `0x134`, RW
- `BinningHorizontal` (Integer -> IntReg) at `0x108`, RW
- `BinningVertical` (Integer -> IntReg) at `0x10c`, RW
- `PixelFormat` (Enumeration -> IntReg) at `0x128`, RW
- `AcquisitionStart`/`AcquisitionStop` (Command -> IntReg) at `0x124`, WO
- `PayloadSize` (SwissKnife), computed from width/height/pixel format

All integer registers in `arv-fake-camera.xml` are big-endian; keep that if you follow its map.

## Minimal XML skeleton

```
<RegisterDescription ... xmlns="http://www.genicam.org/GenApi/Version_1_0">
  <Category Name="Root" NameSpace="Standard">
    <pFeature>DeviceControl</pFeature>
    <pFeature>ImageFormatControl</pFeature>
    <pFeature>AcquisitionControl</pFeature>
    <pFeature>TransportLayerControl</pFeature>
  </Category>

  <Category Name="DeviceControl" NameSpace="Standard">
    <pFeature>DeviceVendorName</pFeature>
    <pFeature>DeviceModelName</pFeature>
    <pFeature>DeviceVersion</pFeature>
    <pFeature>DeviceID</pFeature>
  </Category>

  <StringReg Name="DeviceVendorName"><Address>0x48</Address>...</StringReg>
  <StringReg Name="DeviceModelName"><Address>0x68</Address>...</StringReg>
  <StringReg Name="DeviceVersion"><Address>0x88</Address>...</StringReg>
  <StringReg Name="DeviceID"><Address>0xD8</Address>...</StringReg>

  <Category Name="ImageFormatControl" NameSpace="Standard">
    <pFeature>SensorWidth</pFeature>
    <pFeature>SensorHeight</pFeature>
    <pFeature>OffsetX</pFeature>
    <pFeature>OffsetY</pFeature>
    <pFeature>Width</pFeature>
    <pFeature>Height</pFeature>
    <pFeature>BinningHorizontal</pFeature>
    <pFeature>BinningVertical</pFeature>
    <pFeature>PixelFormat</pFeature>
  </Category>

  <Integer Name="SensorWidth">
    <pValue>SensorWidthRegister</pValue>
  </Integer>
  <IntReg Name="SensorWidthRegister"><Address>0x11c</Address>...</IntReg>

  <Integer Name="SensorHeight">
    <pValue>SensorHeightRegister</pValue>
  </Integer>
  <IntReg Name="SensorHeightRegister"><Address>0x118</Address>...</IntReg>

  <Integer Name="OffsetX">
    <pValue>OffsetXRegister</pValue>
    <Min>0</Min>
    <pMax>SensorWidth</pMax>
    <Inc>1</Inc>
  </Integer>
  <IntReg Name="OffsetXRegister"><Address>0x130</Address>...</IntReg>

  <Integer Name="OffsetY">
    <pValue>OffsetYRegister</pValue>
    <Min>0</Min>
    <pMax>SensorHeight</pMax>
    <Inc>1</Inc>
  </Integer>
  <IntReg Name="OffsetYRegister"><Address>0x134</Address>...</IntReg>

  <Integer Name="Width"><pValue>WidthRegister</pValue>...</Integer>
  <IntReg Name="WidthRegister"><Address>0x100</Address>...</IntReg>

  <Integer Name="Height"><pValue>HeightRegister</pValue>...</Integer>
  <IntReg Name="HeightRegister"><Address>0x104</Address>...</IntReg>

  <Integer Name="BinningHorizontal">
    <pValue>BinningHorizontalRegister</pValue>
    <Min>1</Min>
    <Max>16</Max>
  </Integer>
  <IntReg Name="BinningHorizontalRegister"><Address>0x108</Address>...</IntReg>

  <Integer Name="BinningVertical">
    <pValue>BinningVerticalRegister</pValue>
    <Min>1</Min>
    <Max>16</Max>
  </Integer>
  <IntReg Name="BinningVerticalRegister"><Address>0x10c</Address>...</IntReg>

  <Enumeration Name="PixelFormat">
    <EnumEntry Name="Mono16"><Value>0x01100007</Value></EnumEntry>
    <pValue>PixelFormatRegister</pValue>
  </Enumeration>
  <IntReg Name="PixelFormatRegister"><Address>0x128</Address>...</IntReg>

  <Category Name="AcquisitionControl" NameSpace="Standard">
    <pFeature>AcquisitionStart</pFeature>
    <pFeature>AcquisitionStop</pFeature>
  </Category>
  <Command Name="AcquisitionStart"><pValue>AcquisitionCommandRegister</pValue><CommandValue>1</CommandValue></Command>
  <Command Name="AcquisitionStop"><pValue>AcquisitionCommandRegister</pValue><CommandValue>0</CommandValue></Command>
  <IntReg Name="AcquisitionCommandRegister"><Address>0x124</Address>...</IntReg>

  <Category Name="TransportLayerControl" NameSpace="Standard">
    <pFeature>PayloadSize</pFeature>
  </Category>
  <IntSwissKnife Name="PayloadSize">
    <pVariable Name="WIDTH">Width</pVariable>
    <pVariable Name="HEIGHT">Height</pVariable>
    <pVariable Name="PIXELFORMAT">PixelFormatRegister</pVariable>
    <Formula>WIDTH * HEIGHT * ((PIXELFORMAT>>16)&amp;0xFF) / 8</Formula>
  </IntSwissKnife>

  <Port Name="Device" NameSpace="Standard" />
</RegisterDescription>
```

## Full minimal template (drop-in)

This is a fully-specified minimal XML you can drop in and then populate the backing registers in your backend:

```
<?xml version="1.0" encoding="utf-8"?>

<RegisterDescription
	ModelName="YourModel"
	VendorName="YourVendor"
	StandardNameSpace="None"
	SchemaMajorVersion="1"
	SchemaMinorVersion="0"
	SchemaSubMinorVersion="1"
	MajorVersion="1"
	MinorVersion="0"
	SubMinorVersion="0"
	ToolTip="Minimal GVCP proxy device"
	ProductGuid="0"
	VersionGuid="0"
	xmlns="http://www.genicam.org/GenApi/Version_1_0"
	xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
	xsi:schemaLocation="http://www.genicam.org/GenApi/Version_1_0 GenApiSchema.xsd">

	<Category Name="Root" NameSpace="Standard">
		<pFeature>DeviceControl</pFeature>
		<pFeature>ImageFormatControl</pFeature>
		<pFeature>AcquisitionControl</pFeature>
		<pFeature>TransportLayerControl</pFeature>
	</Category>

	<Category Name="DeviceControl" NameSpace="Standard">
		<pFeature>DeviceVendorName</pFeature>
		<pFeature>DeviceModelName</pFeature>
		<pFeature>DeviceVersion</pFeature>
		<pFeature>DeviceID</pFeature>
	</Category>

	<StringReg Name="DeviceVendorName" NameSpace="Standard">
		<Address>0x48</Address>
		<Length>32</Length>
		<AccessMode>RO</AccessMode>
		<pPort>Device</pPort>
	</StringReg>

	<StringReg Name="DeviceModelName" NameSpace="Standard">
		<Address>0x68</Address>
		<Length>32</Length>
		<AccessMode>RO</AccessMode>
		<pPort>Device</pPort>
	</StringReg>

	<StringReg Name="DeviceVersion" NameSpace="Standard">
		<Address>0x88</Address>
		<Length>32</Length>
		<AccessMode>RO</AccessMode>
		<pPort>Device</pPort>
	</StringReg>

	<StringReg Name="DeviceID" NameSpace="Standard">
		<Address>0xD8</Address>
		<Length>16</Length>
		<AccessMode>RO</AccessMode>
		<pPort>Device</pPort>
	</StringReg>

	<Category Name="ImageFormatControl" NameSpace="Standard">
		<pFeature>SensorWidth</pFeature>
		<pFeature>SensorHeight</pFeature>
		<pFeature>OffsetX</pFeature>
		<pFeature>OffsetY</pFeature>
		<pFeature>Width</pFeature>
		<pFeature>Height</pFeature>
		<pFeature>BinningHorizontal</pFeature>
		<pFeature>BinningVertical</pFeature>
		<pFeature>PixelFormat</pFeature>
	</Category>

	<Integer Name="SensorWidth" NameSpace="Standard">
		<pValue>SensorWidthRegister</pValue>
	</Integer>

	<IntReg Name="SensorWidthRegister" NameSpace="Custom">
		<Address>0x11c</Address>
		<Length>4</Length>
		<AccessMode>RO</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="SensorHeight" NameSpace="Standard">
		<pValue>SensorHeightRegister</pValue>
	</Integer>

	<IntReg Name="SensorHeightRegister" NameSpace="Custom">
		<Address>0x118</Address>
		<Length>4</Length>
		<AccessMode>RO</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="OffsetX" NameSpace="Standard">
		<pValue>OffsetXRegister</pValue>
		<Min>0</Min>
		<pMax>SensorWidth</pMax>
		<Inc>1</Inc>
	</Integer>

	<IntReg Name="OffsetXRegister" NameSpace="Custom">
		<Address>0x130</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="OffsetY" NameSpace="Standard">
		<pValue>OffsetYRegister</pValue>
		<Min>0</Min>
		<pMax>SensorHeight</pMax>
		<Inc>1</Inc>
	</Integer>

	<IntReg Name="OffsetYRegister" NameSpace="Custom">
		<Address>0x134</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="Width" NameSpace="Standard">
		<pValue>WidthRegister</pValue>
		<Min>1</Min>
		<pMax>SensorWidth</pMax>
		<Inc>1</Inc>
	</Integer>

	<IntReg Name="WidthRegister" NameSpace="Custom">
		<Address>0x100</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="Height" NameSpace="Standard">
		<pValue>HeightRegister</pValue>
		<Min>1</Min>
		<pMax>SensorHeight</pMax>
		<Inc>1</Inc>
	</Integer>

	<IntReg Name="HeightRegister" NameSpace="Custom">
		<Address>0x104</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="BinningHorizontal" NameSpace="Standard">
		<pValue>BinningHorizontalRegister</pValue>
		<Min>1</Min>
		<Max>16</Max>
	</Integer>

	<IntReg Name="BinningHorizontalRegister" NameSpace="Custom">
		<Address>0x108</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Integer Name="BinningVertical" NameSpace="Standard">
		<pValue>BinningVerticalRegister</pValue>
		<Min>1</Min>
		<Max>16</Max>
	</Integer>

	<IntReg Name="BinningVerticalRegister" NameSpace="Custom">
		<Address>0x10c</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Enumeration Name="PixelFormat" NameSpace="Standard">
		<EnumEntry Name="Mono16" NameSpace="Standard">
			<Value>0x01100007</Value>
		</EnumEntry>
		<pValue>PixelFormatRegister</pValue>
	</Enumeration>

	<IntReg Name="PixelFormatRegister" NameSpace="Custom">
		<Address>0x128</Address>
		<Length>4</Length>
		<AccessMode>RW</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Category Name="AcquisitionControl" NameSpace="Standard">
		<pFeature>AcquisitionStart</pFeature>
		<pFeature>AcquisitionStop</pFeature>
	</Category>

	<Command Name="AcquisitionStart" NameSpace="Standard">
		<pValue>AcquisitionCommandRegister</pValue>
		<CommandValue>1</CommandValue>
	</Command>

	<Command Name="AcquisitionStop" NameSpace="Standard">
		<pValue>AcquisitionCommandRegister</pValue>
		<CommandValue>0</CommandValue>
	</Command>

	<IntReg Name="AcquisitionCommandRegister" NameSpace="Custom">
		<Address>0x124</Address>
		<Length>4</Length>
		<AccessMode>WO</AccessMode>
		<pPort>Device</pPort>
		<Sign>Unsigned</Sign>
		<Endianess>BigEndian</Endianess>
	</IntReg>

	<Category Name="TransportLayerControl" NameSpace="Standard">
		<pFeature>PayloadSize</pFeature>
	</Category>

	<IntSwissKnife Name="PayloadSize" NameSpace="Standard">
		<pVariable Name="WIDTH">Width</pVariable>
		<pVariable Name="HEIGHT">Height</pVariable>
		<pVariable Name="PIXELFORMAT">PixelFormatRegister</pVariable>
		<Formula>WIDTH * HEIGHT * ((PIXELFORMAT&gt;&gt;16)&amp;0xFF) / 8</Formula>
	</IntSwissKnife>

	<Port Name="Device" NameSpace="Standard">
	</Port>
</RegisterDescription>
```

## Default values for 640x512 Mono16

If you want Aravis to come up with 640x512 Mono16 by default, initialize these registers in your backend:

- `SensorWidthRegister` (`0x11c`): set to full sensor width
- `SensorHeightRegister` (`0x118`): set to full sensor height
- `WidthRegister` (`0x100`): `640`
- `HeightRegister` (`0x104`): `512`
- `OffsetXRegister` (`0x130`): `0`
- `OffsetYRegister` (`0x134`): `0`
- `BinningHorizontalRegister` (`0x108`): `1`
- `BinningVerticalRegister` (`0x10c`): `1`
- `PixelFormatRegister` (`0x128`): `0x01100007` (Mono16)

## Stream destination registers (GVBS)

Even though these are not defined in GenICam XML, your backend must implement them in the GVBS register map for Aravis to configure the GVSP destination:

- `ARV_GVBS_STREAM_CHANNEL_0_IP_ADDRESS_OFFSET`
- `ARV_GVBS_STREAM_CHANNEL_0_PORT_OFFSET`
- `ARV_GVBS_STREAM_CHANNEL_0_PACKET_SIZE_OFFSET`

Those offsets are listed in `src/arvgvcpprivate.h`.
