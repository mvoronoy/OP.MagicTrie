FROM fedora_clang

# Following is commented since set-up obsolete Boost, but still can be useful for a dev-CentOS purpose 
# RUN dnf -y install groupinstall 'Development Tools'

RUN cd /tmp \
      && mkdir -p /tmp/boost \
      && cd /tmp/boost \
      && wget https://dl.bintray.com/boostorg/release/1.74.0/source/boost_1_74_0.tar.bz2 \
      && echo "83bfc1507731a0906e387fc28b7ef5417d591429e51e788417fe9ff025e116b1 boost_1_74_0.tar.bz2" | sha256sum -c

WORKDIR /tmp/boost

# install boost
RUN cd /tmp/boost \
    && tar -xjf boost_1_74_0.tar.bz2 --strip-components 1 \
    && ./bootstrap.sh --with-toolset=clang --prefix=/usr/boost/ 

COPY ./user-config.jam /tmp/boost

RUN ./b2 clean \
     && ./b2 --user-config=user-config.jam  --with-log  --with-python cxxflags=-std=c++17 install --prefix=/usr/boost/

#    && ./b2 --with-log  --with-python  toolset=clang cxxflags="-stdlib=libc++" linkflags="-stdlib=libc++" stage \
#    && ./b2 --with-log  --with-python  toolset=clang cxxflags="-stdlib=libc++" linkflags="-stdlib=libc++" install

RUN rm -rf /tmp/boost

ENV BOOST_ROOT /usr/boost/

WORKDIR /src