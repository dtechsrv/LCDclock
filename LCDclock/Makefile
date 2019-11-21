#
# Copyright (C) 2016 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=lcdclock
PKG_VERSION:=1.2.1
PKG_RELEASE:=r74

include $(INCLUDE_DIR)/package.mk

define Package/lcdclock
	SECTION:=utils
	CATEGORY:=Utilities
	TITLE:=LCDclock
	MAINTAINER:=Gábor Deé <dtech@dtech.hu>
	DEPENDS:=kmod-i2c-gpio-custom kmod-w1-gpio-custom kmod-w1-slave-therm
endef

define Package/lcdclock/description
	LCD Clock for HD44780 16x2 display over I2C by PCF8574 bus expander
	with 1-wire Thermal Sensor extension for DS1820.
endef

define Package/lcdclock/conffiles
/etc/config/lcdclock
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(LDFLAGS) $(LIBGCC_S) -o $(PKG_BUILD_DIR)/lcdclock $(PKG_BUILD_DIR)/lcdclock.c
endef

define Package/lcdclock/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/lcdclock $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_DATA) ./files/lcdclock.conf $(1)/etc/config/lcdclock
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/LCDclock.init $(1)/etc/init.d/LCDclock
endef

$(eval $(call BuildPackage,lcdclock))