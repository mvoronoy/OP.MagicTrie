FROM op_devenv


LABEL description="Container to cross-compile for Linux"

EXPOSE 22


RUN ssh-keygen -N '' -t rsa -f /etc/ssh/ssh_host_rsa_key
RUN ssh-keygen -N '' -t dsa -f /etc/ssh/ssh_host_ecdsa_key
RUN echo root:welcome1 | chpasswd

# run '/usr/sbin/sshd -D' 

WORKDIR /src
# Cannot use: RUN git clone https://github.com/mvoronoy/OP.MagicTrie.git
#   because repo may alredy went ahead
RUN rm -rf OP.MagicTrie && mkdir OP.MagicTrie && cd  "$_" && git init \
    && git remote add origin https://github.com/mvoronoy/OP.MagicTrie.git \
    && git fetch origin \
    && git checkout -b master --track origin/master

RUN cd OP.MagicTrie && \
    cmake -GNinja && \
    cmake --build .

RUN ctest