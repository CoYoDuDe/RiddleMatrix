#ifndef TICKER_H
#define TICKER_H

class Ticker {
  public:
    template <typename Callback>
    void attach(double, Callback) {}

    void detach() {}
};

#endif
