function v = griddyn_unknown_parameter()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 3);
  end
  v = vInitialized;
end
