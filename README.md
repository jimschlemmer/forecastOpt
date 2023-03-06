forecastOpt is a ensemble model weight optimizer.  The models are all global horizontal irradiance (GHI) forecast models.  The optimzier 
works by generating weight sets and applying them to the model input, and assesing the resulting timeseries according to some error 
metric (RMSE, Mean Bias Error, Mean Average Error, or cost).  The primary output of the program is a set of weights associated with the
lowest error for each input time horizon.  
