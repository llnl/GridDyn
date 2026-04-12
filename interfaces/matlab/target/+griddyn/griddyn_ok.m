function v = griddyn_ok()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 0);
  end
  v = vInitialized;
end
