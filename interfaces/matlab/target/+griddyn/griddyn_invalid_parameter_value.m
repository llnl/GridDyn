function v = griddyn_invalid_parameter_value()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 2);
  end
  v = vInitialized;
end
