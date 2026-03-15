#!/bin/bash
echo "=== iptables INPUT chain ==="
sudo iptables -L INPUT -n -v 2>/dev/null

echo ""
echo "=== nftables ==="
sudo nft list ruleset 2>/dev/null

echo ""
echo "=== ufw status ==="
sudo ufw status 2>/dev/null

echo ""
echo "=== firewalld ==="
sudo firewall-cmd --list-all 2>/dev/null
