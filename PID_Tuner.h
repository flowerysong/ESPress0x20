class PID_Tuner {
  public:
    PID_Tuner(float *, float *);
    void reset(void);
    bool run(void);

    float get_p(void);
    float get_i(void);
    float get_d(void); // Noice

  private:
    float *       input;
    float *       output;
    float         setpoint;
    float         noise_band;
    int           sample_time;
    bool          finished;
    unsigned long ts_last;
    float         input_max;
    float         input_min;
    float         input_ring[ 100 ];
    bool          input_valid;
    int           input_idx;
    float         output_high;
    float         output_low;
    int           peak_type;
    float         peaks[ 10 ];
    int           peak_count;
    unsigned long ts_ultimate;
    unsigned long ts_penultimate;
    float         Ku;
    float         Pu;
};
