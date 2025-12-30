FROM alpine:latest

# Install necessary packages
RUN apk --no-cache add iptables iproute2 bash bc

# Copy the firewall script into the container
COPY firewall.sh /usr/local/bin/firewall.sh

# Make the script executable
RUN chmod +x /usr/local/bin/firewall.sh

# Run the firewall script when the container starts
CMD ["/usr/local/bin/firewall.sh"]
